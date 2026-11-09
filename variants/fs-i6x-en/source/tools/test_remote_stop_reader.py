#!/usr/bin/env python3
"""Failure paths must restore the CPU independently of the GDB process."""
import importlib.util
from pathlib import Path
import subprocess
import unittest

path = Path(__file__).with_name('read_remote_stop_diag.py')
spec = importlib.util.spec_from_file_location('remote_stop_reader', path)
reader = importlib.util.module_from_spec(spec)
spec.loader.exec_module(reader)


def capture():
    counts = {key: 0 for key in ['expired_messages', 'queue_overflows', 'stream_resets',
                                'invalid_messages', 'crc_errors', 'invalid_channel_frames', 'uart_errors']}
    return {'verified_flash_sha256': 'fixture', 'locked': True, 'type_size': 232,
            'record': {'magic': reader.MAGIC, 'version': 1, 'record_size': 232,
                       'write_sequence': 2, 'event_count': 1, 'armed': 0, 'valid': 1,
                       'boot_reset_flags': 1 << 29, 'gate_reason': 3, 'latch_reason': 2,
                       'rx_at_authorization': {**counts, 'uart_errors': 5},
                       'rx': {**counts, 'uart_errors': 7},
                       'input_flags': (1 << 3) | (1 << 4)}}


class FakeRpc:
    def __init__(self, fail_resume=False, halt_works=True):
        self.calls = []
        self.running = True
        self.fail_resume = fail_resume
        self.halt_works = halt_works

    def __call__(self, port, command):
        self.calls.append(command)
        if command == 'halt' and self.halt_works:
            self.running = False
        if command == 'resume':
            if self.fail_resume:
                raise OSError('USB disconnected')
            self.running = True
        if command == 'capture {poll}':
            return 'background polling: off\nTAP: stm32f4x.cpu (enabled)\n'
        if command == 'set t [target current]; $t arp_poll; $t curstate':
            return 'running' if self.running else 'halted'
        return ''


class ReaderTests(unittest.TestCase):
    def test_success_resumes(self):
        rpc = FakeRpc()
        result = reader.capture_with_recovery(123, lambda: reader.validate_capture(capture(), 'fixture'), rpc)
        self.assertEqual(result['interpretation']['authorization_latch'], 'timeout')
        self.assertTrue(result['interpretation']['inputs']['timeout_history'])
        self.assertEqual(result['boot_reset_causes'], ['IWDG'])
        self.assertEqual(result['interpretation']['rx_counter_increases_since_unlock']['uart_errors'], 2)
        self.assertTrue(rpc.running)
        self.assertEqual(rpc.calls, ['halt', reader.TARGET_STATE_QUERY, 'resume', reader.TARGET_STATE_QUERY])

    def test_bad_flash_and_records_always_resume(self):
        for field, value in [('magic', 0), ('version', 2), ('record_size', 233),
                             ('write_sequence', 3), ('armed', 1), ('event_count', 0), ('valid', 7)]:
            result = capture()
            result['record'][field] = value
            rpc = FakeRpc()
            with self.subTest(field=field), self.assertRaises(reader.CaptureError):
                reader.capture_with_recovery(123, lambda: reader.validate_capture(result, 'fixture'), rpc)
            self.assertTrue(rpc.running)
        for field, value in [('verified_flash_sha256', 'wrong'), ('locked', False)]:
            result = capture()
            result[field] = value
            rpc = FakeRpc()
            with self.subTest(field=field), self.assertRaises(reader.CaptureError):
                reader.capture_with_recovery(123, lambda: reader.validate_capture(result, 'fixture'), rpc)
            self.assertTrue(rpc.running)

    def test_gdb_failure_timeout_and_interrupt_resume(self):
        for error in [RuntimeError('unstable SRAM'), subprocess.TimeoutExpired('gdb', 1), KeyboardInterrupt()]:
            def fail():
                raise error
            rpc = FakeRpc()
            with self.subTest(error=type(error).__name__), self.assertRaises(type(error)):
                reader.capture_with_recovery(123, fail, rpc)
            self.assertTrue(rpc.running)

    def test_halt_failure_and_resume_failure_are_reported(self):
        rpc = FakeRpc(halt_works=False)
        with self.assertRaises(reader.CaptureError):
            reader.capture_with_recovery(123, lambda: self.fail('capture must not run'), rpc)
        self.assertIn('resume', rpc.calls)
        rpc = FakeRpc(fail_resume=True)
        with self.assertRaises(reader.RecoveryError):
            reader.capture_with_recovery(123, capture, rpc)
        self.assertFalse(rpc.running)

    def test_no_event_after_boot_is_reported_without_inventing_cause(self):
        result = capture()
        result['record'].update(valid=0, event_count=0, write_sequence=0)
        decoded = reader.validate_capture(result, 'fixture')
        self.assertNotIn('authorization_latch', decoded['interpretation'])
        self.assertIn('本次启动', decoded['interpretation']['note'])

    def test_gdb_script_has_only_read_operations(self):
        script = reader.build_gdb_script(1234, Path('/tmp/firmware with spaces.bin'))
        self.assertIn('set trust-readonly-sections off', script)
        self.assertIn("app['remote_stop_diag']", script)
        self.assertIn('first != second', script)
        self.assertIn('actual != expected', script)
        commands = script.split('python\n', 1)[0].splitlines()
        self.assertFalse(any(command.split()[0] in ['load', 'restore', 'call', 'run'] for command in commands))
        self.assertNotIn('monitor reset', script)
        self.assertNotIn('0x2000', script)


if __name__ == '__main__':
    unittest.main()
