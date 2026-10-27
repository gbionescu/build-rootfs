#!/usr/bin/env python3
import random
import unittest
from collections import deque

from snake_logic import (
    DOWN,
    INITIAL_DELAY_MS,
    LEFT,
    MIN_DELAY_MS,
    POINTS_PER_FOOD,
    RIGHT,
    SPEEDUP_EVERY,
    SnakeGame,
    UP,
)


class SnakeGameTest(unittest.TestCase):
    def setUp(self):
        self.game = SnakeGame(grid_size=10, rng=random.Random(1234))

    def test_initial_state(self):
        self.assertEqual(len(self.game.snake), 3)
        self.assertEqual(self.game.snake[0], (5, 5))
        self.assertEqual(self.game.direction, RIGHT)
        self.assertEqual(self.game.score, 0)
        self.assertFalse(self.game.game_over)
        self.assertNotIn(self.game.food, self.game.snake)

    def test_step_moves_forward(self):
        self.game.step()
        self.assertEqual(self.game.snake[0], (6, 5))
        self.assertEqual(len(self.game.snake), 3)
        self.assertNotIn((3, 5), self.game.snake)

    def test_reverse_direction_is_rejected(self):
        self.game.set_direction(LEFT)
        self.assertEqual(self.game.pending_direction, RIGHT)
        self.game.step()
        self.assertEqual(self.game.snake[0], (6, 5))

    def test_two_quick_turns_are_bounded(self):
        self.game.set_direction(UP)
        self.game.set_direction(LEFT)
        self.assertEqual(self.game.pending_direction, UP)
        self.game.step()
        self.assertEqual(self.game.snake[0], (5, 4))

    def test_eating_food_grows_and_scores(self):
        self.game.food = (6, 5)
        self.game.step()
        self.assertEqual(len(self.game.snake), 4)
        self.assertEqual(self.game.score, POINTS_PER_FOOD)
        self.assertNotIn(self.game.food, self.game.snake)

    def test_wall_collision(self):
        game = SnakeGame(grid_size=4, rng=random.Random(7))
        game.food = None
        game.step()
        self.assertFalse(game.game_over)
        game.step()
        self.assertTrue(game.game_over)

    def test_self_collision(self):
        self.game.snake = deque([(1, 1), (1, 2), (2, 2), (2, 1)])
        self.game.direction = DOWN
        self.game.pending_direction = DOWN
        self.game.food = None
        self.game.step()
        self.assertTrue(self.game.game_over)

    def test_food_is_never_placed_on_snake(self):
        for _ in range(100):
            self.assertNotIn(self.game._place_food(), self.game.snake)

    def test_speed_increases_and_is_capped(self):
        self.assertEqual(self.game.speed_delay_ms, INITIAL_DELAY_MS)
        self.game.score = SPEEDUP_EVERY
        self.assertEqual(self.game.speed_delay_ms, INITIAL_DELAY_MS - 10)
        self.assertEqual(self.game.speed_level, 2)
        self.game.score = 10000
        self.assertEqual(self.game.speed_delay_ms, MIN_DELAY_MS)


if __name__ == "__main__":
    unittest.main()
