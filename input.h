/*
 * This file is part of cappls, a screen recorder.
 * Copyright (C) 2025 Joe Desmond
 *
 * cappls is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * cappls is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with cappls.  If not, see <https://www.gnu.org/licenses/>.
 */
#pragma once

// Installs a low-level keyboard hook to listen for the key combo (CTRL+SHIFT+.).
// When the combo is pressed, the keyboard hook calls `on_combo_pressed`.
void install_hook();

// Uninstalls the low-level keyboard hook.
void uninstall_hook();

// Drains the message queue without blocking.
void process_messages();

void on_combo_pressed();