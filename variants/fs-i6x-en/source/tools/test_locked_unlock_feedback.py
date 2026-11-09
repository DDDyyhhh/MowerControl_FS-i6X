#!/usr/bin/env python3
"""Locked-gate/UI coverage is consolidated in the complete profile-3 closed loop.

The harness executes the actual always-run RefreshSafetyOwnershipGate,
DispatchRemoteConfirmation, RemoteAuthorization, SENS and production GUI.
"""
from pathlib import Path
import runpy
runpy.run_path(str(Path(__file__).with_name('test_startup_unlock.py')),run_name='__main__')
