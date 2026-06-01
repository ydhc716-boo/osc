"""
DDS Signal Generator Simulator.

Models the STM32 DDS behavior:
  - 32-bit phase accumulator
  - 1 MHz DAC update rate (Fs)
  - 256-point sine lookup table
  - Square/triangle/sawtooth computed analytically
  - 12-bit DAC output (0–4095)
"""

import math
import numpy as np

# Constants
DAC_BITS    = 12
DAC_MAX     = (1 << DAC_BITS) - 1  # 4095
DAC_MID     = DAC_MAX // 2          # 2047 (zero offset for bipolar signals)
FS          = 1_000_000             # 1 MHz DAC update rate
PHASE_MAX   = 0x1_0000_0000        # 2^32

# Pre-compute 256-point sine LUT (same as MCU firmware)
SINE_LUT_SIZE = 256
SINE_LUT = np.array([
    int(DAC_MID + DAC_MID * math.sin(2 * math.pi * i / SINE_LUT_SIZE))
    for i in range(SINE_LUT_SIZE)
], dtype=np.uint16)


class DDSSignalGenerator:
    """Simulated DDS signal generator."""

    def __init__(self):
        self.waveform = 0         # WAVE_SINE
        self.freq_hz: float = 1000.0
        self.amplitude_mv: int = 3300  # full scale
        self.running = False
        self._phase: int = 0       # 32-bit phase accumulator
        self._sample_counter: int = 0

    @property
    def phase_increment(self) -> int:
        """Compute 32-bit phase increment for current frequency."""
        return int((self.freq_hz * PHASE_MAX) / FS) & 0xFFFFFFFF

    def configure(self, waveform=None, freq_hz=None, amplitude_mv=None):
        """Update configuration. Only changes non-None parameters."""
        if waveform is not None:
            self.waveform = waveform
        if freq_hz is not None:
            self.freq_hz = float(freq_hz)
        if amplitude_mv is not None:
            self.amplitude_mv = int(amplitude_mv)
        # Reset phase on config change for clean waveform
        self._phase = 0

    def start(self):
        self.running = True
        self._phase = 0
        self._sample_counter = 0

    def stop(self):
        self.running = False

    def generate_samples(self, count: int) -> np.ndarray:
        """
        Generate `count` DAC samples at the current settings.
        Returns uint16 numpy array of DAC codes (0–4095).
        """
        if not self.running:
            return np.zeros(count, dtype=np.uint16)

        phase_inc = self.phase_increment
        samples = np.zeros(count, dtype=np.uint16)

        if self.waveform == 0:  # Sine — via LUT
            for i in range(count):
                idx = (self._phase >> 24) & 0xFF  # top 8 bits
                samples[i] = SINE_LUT[idx]
                self._phase = (self._phase + phase_inc) & 0xFFFFFFFF

        elif self.waveform == 1:  # Square
            for i in range(count):
                val = DAC_MAX if (self._phase < PHASE_MAX // 2) else 0
                samples[i] = val
                self._phase = (self._phase + phase_inc) & 0xFFFFFFFF

        elif self.waveform == 2:  # Triangle
            for i in range(count):
                ph = self._phase / PHASE_MAX  # 0.0 to 1.0
                if ph < 0.5:
                    val = int(4 * ph * DAC_MID)
                else:
                    val = int(4 * (1.0 - ph) * DAC_MID)
                samples[i] = val
                self._phase = (self._phase + phase_inc) & 0xFFFFFFFF

        elif self.waveform == 3:  # Sawtooth
            for i in range(count):
                val = int((self._phase / PHASE_MAX) * DAC_MAX)
                samples[i] = val
                self._phase = (self._phase + phase_inc) & 0xFFFFFFFF

        self._sample_counter += count

        # Apply amplitude scaling (scale around midpoint)
        if self.amplitude_mv < 3300:
            scale = self.amplitude_mv / 3300.0
            mid = DAC_MID
            scaled = ((samples.astype(np.float64) - mid) * scale + mid)
            samples = np.clip(scaled, 0, DAC_MAX).astype(np.uint16)

        return samples

    def get_status(self) -> dict:
        return {
            'sg_on': self.running,
            'waveform': self.waveform,
            'freq_hz': int(self.freq_hz),
            'amp_mv': self.amplitude_mv,
        }
