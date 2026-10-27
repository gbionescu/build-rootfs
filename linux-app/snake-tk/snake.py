#!/usr/bin/env python3
import tkinter as tk
from tkinter import font as tkfont

from snake_logic import (
    DOWN,
    GRID_SIZE,
    LEFT,
    RIGHT,
    SnakeGame,
    UP,
)

CELL = 24
CANVAS_SIZE = GRID_SIZE * CELL

BG = "#181825"
GRID_LINE = "#1e1e2e"
SNAKE_BODY = "#a6e3a1"
SNAKE_HEAD = "#89dceb"
FOOD = "#f38ba8"
TEXT = "#cdd6f4"
SUBTEXT = "#a6adc8"
OVERLAY = "#11111b"


class SnakeApp:
    def __init__(self, root):
        self.root = root
        self.game = SnakeGame()
        self.started = False
        self.paused = False
        self.after_id = None

        root.title("Python Snake")
        root.configure(bg=BG)
        root.resizable(False, False)

        header = tk.Frame(root, bg=BG)
        header.pack(fill="x", padx=10, pady=(8, 4))

        self.score_label = tk.Label(
            header, text="Score: 0", bg=BG, fg=TEXT,
            font=tkfont.Font(family="DejaVu Sans Mono", size=12, weight="bold"))
        self.score_label.pack(side="left")

        self.speed_label = tk.Label(
            header, text="Speed: 1", bg=BG, fg=SUBTEXT,
            font=tkfont.Font(family="DejaVu Sans Mono", size=12))
        self.speed_label.pack(side="right")

        self.canvas = tk.Canvas(
            root, width=CANVAS_SIZE, height=CANVAS_SIZE,
            bg=BG, highlightthickness=0)
        self.canvas.pack(padx=10)

        self.help_label = tk.Label(
            root, text="Arrows/WASD move   P/Space pause   R restart   Esc quit",
            bg=BG, fg=SUBTEXT,
            font=tkfont.Font(family="DejaVu Sans Mono", size=9))
        self.help_label.pack(pady=(6, 8))

        root.bind("<Up>", lambda event: self.on_direction(UP))
        root.bind("<Down>", lambda event: self.on_direction(DOWN))
        root.bind("<Left>", lambda event: self.on_direction(LEFT))
        root.bind("<Right>", lambda event: self.on_direction(RIGHT))
        for key, direction in (("w", UP), ("s", DOWN), ("a", LEFT), ("d", RIGHT)):
            root.bind(f"<KeyPress-{key}>", lambda event, d=direction: self.on_direction(d))
            root.bind(f"<KeyPress-{key.upper()}>", lambda event, d=direction: self.on_direction(d))

        root.bind("<space>", lambda event: self.toggle_pause())
        root.bind("<KeyPress-p>", lambda event: self.toggle_pause())
        root.bind("<KeyPress-P>", lambda event: self.toggle_pause())
        root.bind("<KeyPress-r>", lambda event: self.reset())
        root.bind("<KeyPress-R>", lambda event: self.reset())
        root.bind("<Escape>", lambda event: self.close())
        root.bind("<KeyPress-q>", lambda event: self.close())
        root.protocol("WM_DELETE_WINDOW", self.close)

        root.after(200, self.take_focus)
        self.draw()

    def take_focus(self):
        self.root.focus_force()
        self.canvas.focus_set()

    def on_direction(self, direction):
        if self.game.game_over:
            return
        if not self.started:
            self.started = True
            self.paused = False
        self.game.set_direction(direction)
        self.draw()
        self.ensure_timer()

    def toggle_pause(self):
        if not self.started or self.game.game_over:
            return
        self.paused = not self.paused
        if self.paused:
            self.cancel_timer()
        self.draw()
        self.ensure_timer()

    def reset(self):
        self.cancel_timer()
        self.game.reset()
        self.started = False
        self.paused = False
        self.draw()

    def close(self):
        self.cancel_timer()
        self.root.destroy()

    def cancel_timer(self):
        if self.after_id is not None:
            self.root.after_cancel(self.after_id)
            self.after_id = None

    def ensure_timer(self):
        if (self.after_id is None and self.started and not self.paused and
                not self.game.game_over):
            self.after_id = self.root.after(
                self.game.speed_delay_ms, self.tick)

    def tick(self):
        self.after_id = None
        if not self.started or self.paused or self.game.game_over:
            return

        self.game.step()
        self.draw()
        self.ensure_timer()

    def draw(self):
        self.canvas.delete("all")

        for i in range(GRID_SIZE + 1):
            pos = i * CELL
            self.canvas.create_line(pos, 0, pos, CANVAS_SIZE,
                                    fill=GRID_LINE)
            self.canvas.create_line(0, pos, CANVAS_SIZE, pos,
                                    fill=GRID_LINE)

        if self.game.food is not None:
            x, y = self.game.food
            self.canvas.create_oval(
                x * CELL + 5, y * CELL + 5,
                (x + 1) * CELL - 5, (y + 1) * CELL - 5,
                fill=FOOD, outline="")

        for index, (x, y) in enumerate(self.game.snake):
            color = SNAKE_HEAD if index == 0 else SNAKE_BODY
            self.canvas.create_rectangle(
                x * CELL + 2, y * CELL + 2,
                (x + 1) * CELL - 2, (y + 1) * CELL - 2,
                fill=color, outline="")

        self.score_label.config(text=f"Score: {self.game.score}")
        self.speed_label.config(text=f"Speed: {self.game.speed_level}")

        if not self.started:
            self.draw_overlay("Python Snake", "Press arrow keys / WASD to start")
        elif self.paused:
            self.draw_overlay("Paused", "Press P or Space to resume")
        elif self.game.game_over:
            self.draw_overlay(
                "Game Over",
                f"Score: {self.game.score}    Press R to restart")

    def draw_overlay(self, title, subtitle):
        self.canvas.create_rectangle(
            0, 0, CANVAS_SIZE, CANVAS_SIZE,
            fill=OVERLAY, stipple="gray50", outline="")
        title_font = tkfont.Font(family="DejaVu Sans Mono", size=26, weight="bold")
        subtitle_font = tkfont.Font(family="DejaVu Sans Mono", size=11)
        self.canvas.create_text(
            CANVAS_SIZE // 2, CANVAS_SIZE // 2 - 24,
            text=title, fill=TEXT, font=title_font)
        self.canvas.create_text(
            CANVAS_SIZE // 2, CANVAS_SIZE // 2 + 28,
            text=subtitle, fill=SUBTEXT, font=subtitle_font)


def main():
    root = tk.Tk()
    SnakeApp(root)
    root.mainloop()


if __name__ == "__main__":
    main()
