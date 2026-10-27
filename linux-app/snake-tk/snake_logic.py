from collections import deque
import random

GRID_SIZE = 20
INITIAL_DELAY_MS = 140
MIN_DELAY_MS = 60
SPEED_STEP_MS = 10
POINTS_PER_FOOD = 10
SPEEDUP_EVERY = 50

UP = (0, -1)
DOWN = (0, 1)
LEFT = (-1, 0)
RIGHT = (1, 0)

DIRECTIONS = (UP, DOWN, LEFT, RIGHT)


class SnakeGame:
    def __init__(self, grid_size=GRID_SIZE, rng=None):
        self.grid_size = grid_size
        self.rng = rng or random.Random()
        self.reset()

    def reset(self):
        center = self.grid_size // 2
        self.snake = deque([
            (center, center),
            (center - 1, center),
            (center - 2, center),
        ])
        self.direction = RIGHT
        self.pending_direction = RIGHT
        self.score = 0
        self.game_over = False
        self.food = self._place_food()

    def _place_food(self):
        free = [
            (x, y)
            for y in range(self.grid_size)
            for x in range(self.grid_size)
            if (x, y) not in self.snake
        ]
        if not free:
            return None
        return self.rng.choice(free)

    def set_direction(self, direction):
        if direction not in DIRECTIONS or self.game_over:
            return

        opposite = (-self.direction[0], -self.direction[1])
        if direction == opposite or direction == self.direction:
            return

        # Allow only one queued turn per tick.
        if self.pending_direction != self.direction:
            return

        self.pending_direction = direction

    def step(self):
        if self.game_over:
            return

        self.direction = self.pending_direction
        head_x, head_y = self.snake[0]
        dx, dy = self.direction
        new_head = (head_x + dx, head_y + dy)

        if not (0 <= new_head[0] < self.grid_size and
                0 <= new_head[1] < self.grid_size):
            self.game_over = True
            return

        # Moving into the current tail is allowed because the tail moves away.
        if new_head in list(self.snake)[:-1]:
            self.game_over = True
            return

        self.snake.appendleft(new_head)

        if new_head == self.food:
            self.score += POINTS_PER_FOOD
            self.food = self._place_food()
        else:
            self.snake.pop()

    @property
    def speed_delay_ms(self):
        steps = self.score // SPEEDUP_EVERY
        return max(MIN_DELAY_MS, INITIAL_DELAY_MS - steps * SPEED_STEP_MS)

    @property
    def speed_level(self):
        return self.score // SPEEDUP_EVERY + 1
