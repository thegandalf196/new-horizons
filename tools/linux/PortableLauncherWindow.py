"""GTK4 desktop entry for player releases, updates, asset selection and startup status."""
# SPDX-License-Identifier: GPL-2.0-or-later

import os
import threading

import gi
gi.require_version('Gtk', '4.0')
from gi.repository import Gio, GLib, Gtk

import PortableReleaseLauncher as release
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
progressbar trough { background: #342b21; }
progressbar progress { background: #c6a357; }
'''


class LauncherWindow(Gtk.ApplicationWindow):
    def __init__(self, application, original, package):
        super().__init__(application=application, title='New Horizons')
        self.original, self.package = original, package
        self.offer, self.busy, self.closed = None, False, False
        self.kind, self.cancel, self.ready = '', threading.Event(), threading.Event()
        self.game_cancel = StartupCancellation()
        self.set_default_size(560, 330)
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
        self.version_label = Gtk.Label(xalign=0)
        self.version_label.add_css_class('version')
        panel.append(self.version_label)
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
        self.version_label.set_text('Installed version ' + '.'.join(map(str, release.version(self.package))))

    def set_busy(self, busy):
        self.busy = busy
        for button in (self.play, self.check, self.assets):
            button.set_sensitive(not busy)
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
        self.status.set_text('Checking the official release…')
        def checked(offer):
            self.offer = offer
            self.install.set_sensitive(offer is not None)
            self.status.set_text('Version ' + offer['version'] + ' is available.' if offer else 'You have the latest version.')
        self.begin('check', lambda: release.check_release_bounded(self.package, self.cancel), checked)
        return False

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
        def installed(package):
            self.package, self.offer = package, None
            self.refresh_version()
            self.status.set_text('Update installed. Ready to play.')
        self.begin('install', lambda: release.install_release(self.original, self.offer, self.update_progress, self.cancel), installed)

    def assets_clicked(self, button):
        self.status.set_text('Choose your original Heroes III Complete folder…')
        def selected(result):
            if result.returncode:
                raise RuntimeError(result.stderr[-1500:] or 'Original game file selection failed')
            self.status.set_text('Original game file selection completed.')
        self.begin('assets', lambda: run_process(release.setup_command(self.package, ('--select-assets', '--setup-only')),
                                               cancel=self.cancel), selected)

    def play_clicked(self, button):
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
        self.begin('game', lambda: run_process(release.setup_command(self.package), environment,
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


def run_window(original, package):
    application = Gtk.Application(application_id='org.newhorizons.PlayerLauncher', flags=Gio.ApplicationFlags.NON_UNIQUE)
    application.connect('activate', lambda app: LauncherWindow(app, original, package).present())
    return application.run([])
