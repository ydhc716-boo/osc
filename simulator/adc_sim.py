"""
ADC / Oscilloscope Simulator.

Simulates the STM32 ADC sampling:
  - Configurable sample rate (1kSPS – 1MSPS)
  - Trigger modes: auto, rising edge, falling edge
  - 12-bit ADC values (0–4095)
  - Can be fed from the DDS simulator (loopback) or generate a test signal
"""

import time
import math
import numpy as np
from collections import deque

ADC_BITS  = 12
ADC_MAX   = (1 << ADC_BITS) - 1  # 4095
ADC_MID   = ADC_MAX // 2          # 2048


class ADCScope:
    """Simulated ADC / oscilloscope."""

    def __init__(self):
        self.sample_rate: int = 100_000   # 100 kSPS default
        self.trigger_mode: int = 0        # TRIG_AUTO
        self.trigger_level: int = ADC_MID # mid-scale
        self.running: bool = False
        self._dds_source = None           # optional DDSSignalGenerator for loopback
        self._seq: int = 0                # packet sequence number
        self._sample_counter: int = 0
        self._noise_enabled: bool = True
        self._noise_level: float = 5.0    # LSB RMS noise
        self._test_signal_freq: float = 1000.0
        self._test_signal_type: int = 0   # sine

    def configure(self, sample_rate=None, trigger_mode=None, trigger_level_mv=None):
        """Update configuration."""
        if sample_rate is not None:
            self.sample_rate = max(1000, min(1_000_000, int(sample_rate)))
        if trigger_mode is not None:
            self.trigger_mode = int(trigger_mode)
        if trigger_level_mv is not None:
            # Convert mV to ADC code (3.3V reference)
            self.trigger_level = int(int(trigger_level_mv) * ADC_MAX / 3300)
            self.trigger_level = max(0, min(ADC_MAX, self.trigger_level))

    def set_dds_source(self, dds):
        """Connect a DDS generator as loopback source."""
        self._dds_source = dds

    def start(self):
        self.running = True
        self._seq = 0

    def stop(self):
        self.running = False

    def _generate_test_signal(self, count: int) -> np.ndarray:
        """Generate a test waveform for standalone (non-loopback) mode."""
        t = np.arange(count) / self.sample_rate
        t += self._sample_counter / self.sample_rate
        freq = self._test_signal_freq
        mid = ADC_MID
        amp = ADC_MID * 0.9  # 90% of half-scale

        if self._test_signal_type == 0:  # sine
            signal = mid + amp * np.sin(2 * math.pi * freq * t)
        elif self._test_signal_type == 1:  # square
            signal = mid + amp * np.sign(np.sin(2 * math.pi * freq * t))
        elif self._test_signal_type == 2:  # triangle
            phase = (freq * t) % 1.0
            signal = mid + amp * (4 * np.abs(phase - 0.5) - 1)
        else:  # sawtooth
            phase = (freq * t) % 1.0
            signal = mid + amp * (2 * phase - 1)

        if self._noise_enabled:
            signal += np.random.normal(0, self._noise_level, count)

        return np.clip(signal, 0, ADC_MAX).astype(np.uint16)

    def acquire(self, count: int = 512) -> tuple:
        """
        Acquire `count` samples.
        Returns (seq, samples_uint16_array).
        """
        if not self.running:
            return self._seq, np.zeros(count, dtype=np.uint16)

        if self._dds_source is not None:
            if self._dds_source.running:
                # Loopback mode: sample the DDS output at our sample rate
                # The DDS generates at 1MHz, we decimate to our sample rate
                dds_samples_needed = max(count, int(count * 1_000_000 / self.sample_rate) + 10)
                dds_output = self._dds_source.generate_samples(dds_samples_needed)
                decimation = max(1, 1_000_000 // self.sample_rate)
                samples = dds_output[::decimation][:count].copy()
                if len(samples) < count:
                    samples = np.pad(samples, (0, count - len(samples)), 'edge')
            else:
                # Loopback mode but DDS stopped: read mid-scale (DAC output = 0)
                samples = np.full(count, ADC_MID, dtype=np.uint16)
        else:
            # Standalone mode (no DDS): generate demo test signal
            samples = self._generate_test_signal(count)

        self._sample_counter += count
        seq = self._seq
        self._seq = (self._seq + 1) & 0xFFFF
        return seq, samples.astype(np.uint16)

    def get_status(self) -> dict:
        return {
            'scope_on': self.running,
            'sample_rate': self.sample_rate,
            'trigger_mode': self.trigger_mode,
            'trigger_level_mv': int(self.trigger_level * 3300 / ADC_MAX),
        }
