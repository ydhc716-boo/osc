"""
Re-export protocol from simulator package for standalone web_ui usage.
"""
import sys
import os
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

# flake8: noqa
from simulator.protocol import *  # noqa: E402, F403
