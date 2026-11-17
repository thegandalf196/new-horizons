# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic runner-contract tests; never execute native binaries or game profiles."""
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
import xml.etree.ElementTree as ET

# Support direct, discovery and package-style unittest entrypoints.
sys.path.insert(0, str(Path(__file__).resolve().parent))

import nh_ordinary_decision_benchmark as guard
import nh_native_acceptance_workloads as workloads
import nh_native_benchmark_ci as benchmark_ci


class DiscoveryContractTest(unittest.TestCase):
    def test_parameter_comments_and_disabled_names_remain_literal(self):
        text = ('Running main()\nTyped/Suite/0.  # TypeParam = int\n'
                '  Case/2  # GetParam() = 3\nDISABLED_Suite.\n  DISABLED_Case\n')
        self.assertEqual(guard.listed_tests(text),
                         ['Typed/Suite/0.Case/2', 'DISABLED_Suite.DISABLED_Case'])

    def test_expected_inventory_rejects_missing_wanted_duplicates_and_invalid_shapes(self):
        for expected in [[], ['Suite.Other'], ['Suite.Wanted'] * 2, 'Suite.Wanted', [42]]:
            with self.subTest(expected=expected), self.assertRaises(ValueError):
                guard.expected_discovery_cases({'expectedDiscoveryCases': expected}, 'Suite.Wanted', True)

    def test_gate_requires_inventory_and_non_gate_reports_unverified(self):
        with self.assertRaises(ValueError):
            guard.discovery_membership('Suite.\n  Wanted\n', {}, 'Suite.Wanted', True)
        result = guard.discovery_membership('Suite.\n  Wanted\n  Extra\n', {}, 'Suite.Wanted', False)
        self.assertFalse(result['verified'])
        self.assertEqual(result['scope'], 'Membership unverified')

    def test_exact_inventory_rejects_missing_extra_prefixes_and_duplicate_discovery(self):
        candidate = {'expectedDiscoveryCases': ['Suite.Wanted', 'Suite.Other']}
        for text in ['Suite.\n  Wanted\n', 'Suite.\n  Wanted\n  Other\n  Extra\n',
                     'Suite.\n  WantedExtra\n  Other\n', 'SuiteTestExtra.\n  Wanted\n',
                     'Suite.\n  Wanted\n  Other\n  Wanted\n', 'noise only']:
            with self.subTest(text=text), self.assertRaises(ValueError):
                guard.discovery_membership(text, candidate, 'Suite.Wanted', True)
        self.assertTrue(guard.discovery_membership('Suite.\n  Other\n  Wanted\n', candidate,
                                                  'Suite.Wanted', True)['verified'])


class XmlContractTest(unittest.TestCase):
    def read(self, data, expected='Suite.Case'):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'native.xml'
            path.write_text(data)
            return workloads.read_result(path, expected)

    def test_requires_run_completed_and_exactly_one_successful_case(self):
        for status, result in [('notrun', 'suppressed'), ('run', 'skipped'), ('run', ''), ('', 'completed')]:
            with self.subTest(status=status, result=result), self.assertRaises(ValueError):
                self.read(f'<testsuite><testcase classname="Suite" name="Case" status="{status}" result="{result}"/></testsuite>')
        for data in ['<testsuites/>', '<testsuite><testcase/><testcase/></testsuite>',
                     '<testsuite><testcase classname="Wrong" name="Case" status="run" result="completed"/></testsuite>']:
            with self.subTest(data=data), self.assertRaises(ValueError):
                self.read(data)

    def test_failure_error_skip_and_ambiguous_properties_reject(self):
        rows = ['<failure/>', '<error/>', '<skipped/>',
                '<properties><property name="same" value="1"/><property name="same" value="2"/></properties>',
                '<properties><property value="2"/></properties>', '<properties><property name="noValue"/></properties>']
        for row in rows:
            with self.subTest(row=row), self.assertRaises(ValueError):
                self.read('<testsuite><testcase classname="Suite" name="Case" status="run" result="completed">'
                          + row + '</testcase></testsuite>')

    def test_completed_disabled_case_properties_are_literal(self):
        self.assertEqual(self.read('<testsuite><testcase classname="DISABLED_Suite" name="DISABLED_Case" '
                                   'status="run" result="completed"><properties><property name="count" value="2"/>'
                                   '</properties></testcase></testsuite>', 'DISABLED_Suite.DISABLED_Case'), {'count': '2'})


class CiHarnessPinTest(unittest.TestCase):
    def test_property_schema_pin_is_required_before_runner_execution(self):
        for include_schema in (False, True):
            with self.subTest(include_schema=include_schema), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                paths = [Path(benchmark_ci.__file__).resolve(), Path(guard.__file__).resolve(),
                         Path(workloads.__file__).resolve(), Path(sys.executable).resolve()]
                if include_schema:
                    paths.append(Path(workloads.work_properties.__file__).resolve())
                plan = {'harnessPins': {str(path): guard.digest(path) for path in paths}, 'candidates': {}}
                manifest = root / 'manifest.json'
                manifest.write_text(json.dumps(plan))
                output = root / 'results'
                argv = ['ci', '--manifest', str(manifest), '--manifest-sha256', guard.digest(manifest),
                        '--output', str(output), '--workload', 'castle',
                        '--max-regression-percent', '10', '--execute']
                with patch.object(sys, 'argv', argv), patch.object(guard, 'quiet_lease'), \
                     patch.object(benchmark_ci.subprocess, 'run') as run:
                    run.return_value.returncode = 0
                    self.assertEqual(benchmark_ci.main(), 0 if include_schema else 2)
                    self.assertEqual(run.call_count, 1 if include_schema else 0)
                receipt = json.loads((output / 'ci-result.json').read_text())
                self.assertEqual(receipt['status'], 'PASSED' if include_schema else 'NOT_RUN')
                if not include_schema:
                    self.assertIn('property schema', receipt['error'])


class SyntheticCliTest(unittest.TestCase):
    def run_cli(self, module, tmp, *, gate=True, inventory=True, candidate_time=110, extra=False, parity=True, property_rows=None):
        selector = guard.FILTER if module is guard else workloads.WORKLOADS['castle'][0]
        plan = {'pairs': 3 if gate else 1, 'candidates': {}}
        for arm in ['baseline', 'candidate']:
            plan['candidates'][arm] = {'binary': arm, 'environment': {}}
            if inventory:
                plan['candidates'][arm]['expectedDiscoveryCases'] = [selector]
        manifest = tmp / 'manifest.json'
        manifest.write_text(json.dumps(plan))
        output = tmp / 'results'
        calls = []

        def fake_invoke(candidate, argv, dest, _plan, diagnostics):
            calls.append(argv)
            if '--gtest_list_tests' in argv:
                suite, case = selector.split('.', 1)
                (dest / 'child.log').write_text(suite + '.\n  ' + case + '\n' + ('  Extra\n' if extra else ''))
                return
            root = ET.Element('testsuite')
            suite, case = selector.split('.', 1)
            node = ET.SubElement(root, 'testcase', classname=suite, name=case, status='run', result='completed')
            properties = ET.SubElement(node, 'properties')
            elapsed = str(candidate_time if candidate['binary'] == 'candidate' else 100)
            values = {key: 'stable' for key in guard.PARITY} if module is guard else {'initialEvidence': 'stable'}
            if not parity and candidate['binary'] == 'candidate':
                values['effectiveInputs' if module is guard else 'initialEvidence'] = 'different'
            values.update({'firstCallbackUs': elapsed, 'diagnostics': '0'} if module is guard else {'encounterUs': elapsed})
            if property_rows:
                values.update(property_rows[candidate['binary']])
            for key, value in values.items():
                ET.SubElement(properties, 'property', name=key, value=value)
            ET.ElementTree(root).write(dest / 'native.xml')

        argv = ['runner', '--manifest', str(manifest), '--manifest-sha256', guard.digest(manifest),
                '--output', str(output), '--execute']
        if gate:
            argv += ['--gate', '--max-regression-percent', '10']
        if module is workloads:
            argv += ['--workload', 'castle']
        with patch.object(sys, 'argv', argv), patch.object(guard, 'invoke', side_effect=fake_invoke):
            module.main()
        return json.loads((output / 'summary.json').read_text()), calls

    def test_both_gate_clis_keep_exact_relative_boundary_and_unfiltered_listing(self):
        for module in [guard, workloads]:
            with self.subTest(module=module.__name__), tempfile.TemporaryDirectory() as tmp:
                summary, calls = self.run_cli(module, Path(tmp))
                self.assertTrue(summary['regressionGate']['passed'])
                self.assertTrue(summary['discoveryMembershipVerified'])
                self.assertFalse(summary['candidateGameplayAccepted'])
                self.assertTrue(all(argv == ['--gtest_list_tests'] for argv in calls if '--gtest_list_tests' in argv))
                if module is workloads:
                    self.assertTrue(all('--gtest_also_run_disabled_tests' in argv for argv in calls
                                        if '--gtest_list_tests' not in argv))

    def test_both_gate_clis_still_fail_above_relative_margin(self):
        for module in [guard, workloads]:
            with self.subTest(module=module.__name__), tempfile.TemporaryDirectory() as tmp:
                with self.assertRaises(SystemExit) as caught:
                    self.run_cli(module, Path(tmp), candidate_time=111)
                self.assertEqual(caught.exception.code, 2)

    def test_gate_missing_inventory_and_extra_listing_reject_before_body(self):
        for module in [guard, workloads]:
            for kwargs in [{'inventory': False}, {'extra': True}]:
                with self.subTest(module=module.__name__, kwargs=kwargs), tempfile.TemporaryDirectory() as tmp:
                    with self.assertRaises(ValueError):
                        self.run_cli(module, Path(tmp), **kwargs)

    def test_both_clis_preserve_exact_semantic_parity_rejection(self):
        for module in [guard, workloads]:
            with self.subTest(module=module.__name__), tempfile.TemporaryDirectory() as tmp:
                with self.assertRaises(ValueError):
                    self.run_cli(module, Path(tmp), parity=False)

    def test_unknown_counter_suffixes_are_semantic_cli_failures(self):
        for suffix in ('Exchanges', 'TargetProbes', 'ExchangeCount'):
            values = {arm: {'futureSemantic' + suffix: value}
                      for arm, value in (('baseline', '1'), ('candidate', '2'))}
            with self.subTest(suffix=suffix), tempfile.TemporaryDirectory() as tmp:
                with self.assertRaisesRegex(ValueError, 'Exact semantic parity'):
                    self.run_cli(workloads, Path(tmp), property_rows=values)

    def diagnostic_values(self):
        values = {}
        for side in ('side0', 'side1'):
            values.update({side + suffix: '0' for suffix in
                           workloads.work_properties.FORECAST_WORK + workloads.work_properties.BUDGET_WORK
                           + workloads.work_properties.NATIVE_WORK + workloads.work_properties.CONTROLS})
            for suffix in ('DiagnosticsVersion', 'DiagnosticsSupported', 'BudgetStartedDecisions',
                           'BudgetCompletedFiniteDecisions', 'BudgetProfileMask', 'BudgetFullDecisions'):
                values[side + suffix] = '1'
        return values

    def test_known_work_changes_pass_with_valid_controls(self):
        values = {arm: self.diagnostic_values() for arm in ('baseline', 'candidate')}
        for key in ('side0MovementExchanges', 'side1TargetProbes', 'movementExchangeCount',
                    'side0ForecastRootCaptures', 'side0BudgetSpentGeneration',
                    'side1ForecastNativeReadMaterializations', 'side0ForecastNativeOwningLoans',
                    'side0ForecastNativeQueryRebindings'):
            values['baseline'][key], values['candidate'][key] = '20', '3'
        with tempfile.TemporaryDirectory() as tmp:
            summary, _ = self.run_cli(workloads, Path(tmp), property_rows=values)
            self.assertTrue(summary['exactSemanticParity'])
            self.assertTrue(summary['regressionGate']['passed'])
            rows = json.loads((Path(tmp) / 'results/samples.json').read_text())
            self.assertEqual(rows[0]['diagnosticControls']['side0DiagnosticsSupported'], '1')

    def test_wrong_or_missing_diagnostic_controls_fail_even_when_arms_agree(self):
        for key, value in (('side0DiagnosticsSupported', '0'), ('side1DiagnosticsVersion', '2'),
                           ('side0BudgetProfileMask', '4'), ('side1BudgetCompletedFiniteDecisions', '0'),
                           ('side0BudgetUnsupportedDecisions', '1'), ('side0ForecastSaturated', '1'),
                           ('side0DiagnosticsSupported', None), ('policy', 'Fast/BeamOneTwoActors')):
            values = {arm: self.diagnostic_values() for arm in ('baseline', 'candidate')}
            for fields in values.values():
                if value is None:
                    del fields[key]
                else:
                    fields[key] = value
            with self.subTest(key=key, value=value), tempfile.TemporaryDirectory() as tmp:
                with self.assertRaises(ValueError):
                    self.run_cli(workloads, Path(tmp), property_rows=values)

    def test_original_schema_without_native_extension_remains_supported(self):
        values = {arm: self.diagnostic_values() for arm in ('baseline', 'candidate')}
        for fields in values.values():
            for side in ('side0', 'side1'):
                for suffix in workloads.work_properties.NATIVE_WORK:
                    del fields[side + suffix]
        with tempfile.TemporaryDirectory() as tmp:
            summary, _ = self.run_cli(workloads, Path(tmp), property_rows=values)
            self.assertTrue(summary['exactSemanticParity'])

    def test_partial_native_work_extension_rejects(self):
        values = {arm: self.diagnostic_values() for arm in ('baseline', 'candidate')}
        for fields in values.values():
            del fields['side1ForecastNativeOwningLoans']
        with tempfile.TemporaryDirectory() as tmp, self.assertRaises(ValueError):
            self.run_cli(workloads, Path(tmp), property_rows=values)

    def test_non_gate_cli_keeps_fixture_route_explicitly_unverified(self):
        for module in [guard, workloads]:
            with self.subTest(module=module.__name__), tempfile.TemporaryDirectory() as tmp:
                summary, _ = self.run_cli(module, Path(tmp), gate=False, inventory=False)
                self.assertFalse(summary['discoveryMembershipVerified'])
                self.assertFalse(summary['candidateGameplayAccepted'])


if __name__ == '__main__':
    unittest.main()
