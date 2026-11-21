"""GTK4 desktop entry for player releases, updates, asset selection and startup status."""
# SPDX-License-Identifier: GPL-2.0-or-later

import os
import threading

import gi
gi.require_version('Gtk', '4.0')
from gi.repository import Gio, GLib, Gtk

import PortableReleaseLauncher as release
import PortableReleaseNotes as release_notes
from PortableLauncherProcess import StartupCancellation, run_process


READY = 'NH_LAUNCHER_READY_V1'
PREPARING = 'NH_LAUNCHER_PREPARING_ART_V1'
STYLE = b'''
window { background: #211b16; color: #e9dfcc; }
.title { color: #dfc27c; font-family: serif; font-size: 27px; font-weight: bold; }
.version { color: #c5b994; }
button { background: #42372a; color: #f0e3c3; border: 1px solid #8c7446; padding: 8px 13px; }
button:hover { background: #594832; }
button:disabled { color: #8a8273; border-color: #504839; }
.play { background: #665126; font-weight: bold; }
.beta { color: #9fc6d6; }
popover > contents { background: #2b231b; color: #e9dfcc; border: 1px solid #8c7446; }
popover listview, popover row { background: transparent; color: #e9dfcc; }
popover row:hover, popover row:selected { background: #594832; }
progressbar trough { background: #342b21; }
progressbar progress { background: #c6a357; }
textview, textview text { background: #2b231b; color: #e9dfcc; }
'''


class LauncherWindow(Gtk.ApplicationWindow):
    def __init__(self, application, original, package):
        super().__init__(application=application, title='New Horizons')
        self.original, self.package, self.channel = original, package, 'stable'
        self.packages = {'stable': package, 'beta': release.choose_runtime(original, 'beta')}
        self.offer, self.busy, self.closed = None, False, False
        # Notes from this session's last successful check of each track.
        self.fetched = {'stable': None, 'beta': None}
        self.kind, self.cancel, self.ready = '', threading.Event(), threading.Event()
        self.game_cancel = StartupCancellation()
        self.set_default_size(560, 370)
        self.set_resizable(False)
        self.connect('close-request', self.close_requested)
        css = Gtk.CssProvider()
        css.load_from_data(STYLE)
        Gtk.StyleContext.add_provider_for_display(self.get_display(), css, Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION)
        panel = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=14)
        for side in ('top', 'bottom', 'start', 'end'):
            getattr(panel, 'set_margin_' + side)(22)
        self.set_child(panel)
        title = Gtk.Label(label='NEW HORIZONS', xalign=0)
        title.add_css_class('title')
        panel.append(title)
        track = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=10)
        track.append(Gtk.Label(label='Version track', xalign=0))
        self.track = Gtk.DropDown.new_from_strings(['Stable', 'Beta (experimental, separate saves)'])
        self.track.set_hexpand(True)
        self.track.connect('notify::selected', self.track_changed)
        track.append(self.track)
        panel.append(track)
        version_row = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=10)
        self.version_label = Gtk.Label(xalign=0, hexpand=True)
        self.version_label.add_css_class('version')
        version_row.append(self.version_label)
        self.notes = Gtk.Button(label="What's new")
        self.notes.connect('clicked', self.notes_clicked)
        version_row.append(self.notes)
        panel.append(version_row)
        self.refresh_version()
        self.status = Gtk.Label(label='Ready to play.', xalign=0, wrap=True)
        self.status.set_size_request(-1, 48)
        panel.append(self.status)
        self.progress = Gtk.ProgressBar(visible=False)
        panel.append(self.progress)
        controls = Gtk.Grid(column_spacing=10, row_spacing=10)
        panel.append(controls)
        self.play = self.button(controls, 'Play', self.play_clicked, 0, 0)
        self.play.add_css_class('play')
        self.check = self.button(controls, 'Check for updates', self.check_clicked, 1, 0)
        self.install = self.button(controls, 'Install update', self.install_clicked, 0, 1)
        self.assets = self.button(controls, 'Original game files', self.assets_clicked, 1, 1)
        self.cancel_button = Gtk.Button(label='Cancel operation', visible=False)
        self.cancel_button.connect('clicked', self.cancel_clicked)
        panel.append(self.cancel_button)
        self.set_busy(False)
        if release.automatic_checks():
            GLib.idle_add(self.check_clicked, None)

    def button(self, grid, label, callback, column, row):
        button = Gtk.Button(label=label, hexpand=True)
        button.connect('clicked', callback)
        grid.attach(button, column, row, 1, 1)
        return button

    def refresh_version(self):
        package = self.packages[self.channel]
        if self.channel == 'stable':
            self.version_label.set_text('Installed version ' + '.'.join(map(str, release.version(package))))
            self.version_label.remove_css_class('beta')
            return
        self.version_label.add_css_class('beta')
        self.version_label.set_text('Beta ' + '.'.join(map(str, release.version(package))) + ' · separate saves'
                                    if package else 'No beta installed yet. Check for updates to find one.')

    def track_changed(self, dropdown, parameter):
        if self.busy:
            dropdown.set_selected(0 if self.channel == 'stable' else 1)
            return
        self.channel = 'beta' if dropdown.get_selected() == 1 else 'stable'
        self.package, self.offer = self.packages[self.channel], None
        self.refresh_version()
        self.set_busy(False)
        self.status.set_text('Beta builds try experimental changes and keep their own saves and settings.'
                             if self.channel == 'beta' else 'Ready to play.')
        if self.channel == 'beta' and self.package is None:
            self.check_clicked(None)

    def set_busy(self, busy):
        self.busy = busy
        for button in (self.check, self.assets, self.notes):
            button.set_sensitive(not busy)
        self.play.set_sensitive(not busy and self.package is not None)
        self.track.set_sensitive(not busy)
        self.install.set_sensitive(not busy and self.offer is not None)
        self.cancel_button.set_visible(busy and self.kind in ('game', 'install', 'assets')
                                       and not (self.kind == 'game' and self.ready.is_set()))
        self.progress.set_visible(busy)
        if busy:
            self.progress.pulse()

    def begin(self, kind, operation, finished):
        if self.busy:
            return
        self.kind = kind
        self.cancel.clear()
        self.set_busy(True)
        self.get_application().hold()
        def work():
            try:
                result, error = operation(), None
            except Exception as caught:
                result, error = None, caught
            GLib.idle_add(self.complete, result, error, finished)
        threading.Thread(target=work, daemon=True).start()

    def complete(self, result, error, finished):
        self.set_busy(False)
        try:
            if error:
                self.status.set_text(str(error))
            else:
                finished(result)
        except Exception as caught:
            self.status.set_text(str(caught))
        application = self.get_application()
        application.release()
        if self.closed:
            application.quit()
        return False

    def status_changed(self, text):
        GLib.idle_add(self.status.set_text, text)

    def check_clicked(self, button):
        if self.busy:
            return False
        channel = self.channel
        self.status.set_text('Checking for a beta release…' if channel == 'beta' else 'Checking the official release…')
        operation = self.track_check(channel)
        self.begin('check', operation, lambda result: self.checked(channel, *result))
        return False

    def track_check(self, channel):
        baseline, installed = release.track_baseline(self.original, channel), self.installed_version(channel)
        return lambda: release.check_release_notes_bounded(baseline, installed, self.cancel, channel)

    def installed_version(self, channel):
        package = self.packages[channel]
        return '.'.join(map(str, release.version(package))) if package else None

    def checked(self, channel, offer, notes):
        if channel != self.channel:
            return
        self.offer = offer
        self.fetched[channel] = {**(self.fetched[channel] or {}), **notes}
        self.install.set_sensitive(offer is not None)
        if offer:
            self.status.set_text(('Beta ' if channel == 'beta' else 'Version ') + offer['version'] + ' is available.')
        elif channel == 'beta':
            self.status.set_text('No beta newer than your installed versions is available right now.')
        else:
            self.status.set_text('You have the latest version.')

    def notes_clicked(self, button):
        """Show this track's notes, checking once per session; offline shows the bundled notes."""
        if self.busy:
            return
        channel = self.channel
        if self.fetched[channel] is not None:
            self.show_notes(channel)
            return
        check = self.track_check(channel)
        def operation():
            try:
                return check(), None
            except Exception as error:
                return None, error
        def finished(result):
            if channel != self.channel:
                return
            if result[0] is not None:
                self.checked(channel, *result[0])
            else:
                self.status.set_text('Could not reach the release server. Showing the notes included with this version.')
            self.show_notes(channel)
        self.status.set_text('Loading release notes…')
        self.begin('notes', operation, finished)

    def show_notes(self, channel):
        installed = self.installed_version(channel)
        text = release_notes.compose(channel, installed, self.offer['version'] if self.offer else None,
                                     self.fetched[channel], release_notes.bundled(self.packages[channel], installed))
        NotesWindow(self, 'Beta' if channel == 'beta' else 'Stable', text).present()

    def update_progress(self, value):
        phase = value['phase']
        if phase == 'download':
            received, total = value.get('received', 0), value.get('total', 0)
            if isinstance(received, int) and isinstance(total, int) and 0 <= received <= total and total > 0:
                GLib.idle_add(self.progress.set_fraction, received / total)
                self.status_changed(f'Downloading update… {received // 1048576} / {total // 1048576} MB')
        else:
            self.status_changed({'check': 'Verifying the official release…', 'extract': 'Extracting update…',
                                 'verify': 'Verifying package files…', 'probe': 'Checking the updated game…'}.get(phase, 'Preparing update…'))

    def install_clicked(self, button):
        if self.offer is None or self.busy:
            return
        self.status.set_text('Preparing the selected update…')
        channel = self.offer.get('channel', 'stable')
        def installed(package):
            self.packages[channel] = package
            if channel == self.channel:
                self.package = package
            self.offer = None
            self.refresh_version()
            self.status.set_text('Update installed. Ready to play.')
        self.begin('install', lambda: release.install_release(self.original, self.offer, self.update_progress, self.cancel), installed)

    def assets_clicked(self, button):
        self.status.set_text('Choose your original Heroes III Complete folder…')
        def selected(result):
            if result.returncode:
                raise RuntimeError(result.stderr[-1500:] or 'Original game file selection failed')
            self.status.set_text('Original game file selection completed.')
        package, channel = self.package or self.packages['stable'], self.channel
        self.begin('assets', lambda: run_process(release.setup_command(package, ('--select-assets', '--setup-only'), channel),
                                               cancel=self.cancel), selected)

    def play_clicked(self, button):
        if self.package is None:
            return
        package, channel = self.package, self.channel
        self.game_cancel = StartupCancellation()
        self.ready = self.game_cancel.ready
        self.status.set_text('Checking original game files…')
        environment = dict(os.environ)
        environment.pop('NH_PORTABLE_UPDATE_REQUEST', None)
        environment['NH_LAUNCHER_STATUS'] = '1'
        environment['NH_LAUNCHER_MANAGES_UPDATES'] = '1'
        def output(line):
            if line == PREPARING:
                self.status_changed('Preparing artwork and starting the game…')
            elif line == READY:
                self.game_cancel.mark_ready()
                GLib.idle_add(self.cancel_button.set_visible, False)
                self.status_changed('Game running.')
        def finished(result):
            if result.returncode:
                self.status.set_text('Game exited with code ' + str(result.returncode) + '.\n' + result.stderr[-1000:])
            else:
                self.status.set_text('Game closed.' if self.ready.is_set() else 'Launch cancelled before the game opened.')
        self.begin('game', lambda: run_process(release.setup_command(package, channel=channel), environment,
                                             on_stdout=output, cancel=self.game_cancel), finished)

    def cancel_clicked(self, button):
        if self.kind == 'game':
            self.game_cancel.set()
        else:
            self.cancel.set()

    def close_requested(self, window):
        if self.busy:
            self.closed = True
            if self.kind != 'game' or not self.ready.is_set():
                self.cancel_clicked(None)
            self.set_visible(False)
            return True
        return False


class NotesWindow(Gtk.Window):
    """Read-only plain text: the buffer never interprets markup or links."""
    def __init__(self, parent, track, text):
        super().__init__(title="What's new · " + track, transient_for=parent, modal=True)
        self.set_default_size(560, 460)
        panel = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=12)
        for side in ('top', 'bottom', 'start', 'end'):
            getattr(panel, 'set_margin_' + side)(16)
        self.set_child(panel)
        view = Gtk.TextView(editable=False, cursor_visible=False, wrap_mode=Gtk.WrapMode.WORD_CHAR)
        for side in ('top', 'bottom', 'left', 'right'):
            getattr(view, 'set_' + side + '_margin')(8)
        view.get_buffer().set_text(text)
        scroller = Gtk.ScrolledWindow(vexpand=True, hscrollbar_policy=Gtk.PolicyType.NEVER)
        scroller.set_child(view)
        panel.append(scroller)
        close = Gtk.Button(label='Close', halign=Gtk.Align.END)
        close.connect('clicked', lambda button: self.close())
        panel.append(close)
        self.set_default_widget(close)
        keys = Gtk.ShortcutController()
        keys.add_shortcut(Gtk.Shortcut.new(Gtk.ShortcutTrigger.parse_string('Escape'),
                                           Gtk.NamedAction.new('window.close')))
        self.add_controller(keys)


def run_window(original, package):
    application = Gtk.Application(application_id='org.newhorizons.PlayerLauncher', flags=Gio.ApplicationFlags.NON_UNIQUE)
    application.connect('activate', lambda app: LauncherWindow(app, original, package).present())
    return application.run([])
