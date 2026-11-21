import unittest

import torch

from models import DifferentiableSourceRollout


class AirRolloutTest(unittest.TestCase):
    def test_rising_velocity_reduces_air_acceleration(self):
        rollout = DifferentiableSourceRollout()
        ones = torch.ones((2, 16))
        zeros = torch.zeros((2, 16))
        init = torch.tensor([
            [0, 0, 100, 300, 0, 0, 0, 0, 512],
            [0, 0, 300, 300, 0, 0, 0, 0, 512],
        ], dtype=torch.float32)
        with torch.no_grad():
            pos, _ = rollout(ones, zeros, zeros, zeros, zeros, init, True)
        self.assertAlmostEqual(pos[0, 0, 0].item(), 0.16875, places=5)
        self.assertAlmostEqual(pos[1, 0, 0].item(), 0.45, places=5)


if __name__ == "__main__":
    unittest.main()
