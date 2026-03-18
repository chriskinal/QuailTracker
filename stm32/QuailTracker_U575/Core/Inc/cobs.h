/*
 * QuailTracker - GPS-synchronized Autonomous Recording Unit
 * Copyright (C) 2026 QuailTracker Project
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/* COBS (Consistent Overhead Byte Stuffing) encoder/decoder.
 * Encodes data so no zero bytes appear, using 0x00 as frame delimiter.
 * Self-synchronizing: on corruption, skip to next 0x00 and retry. */

#ifndef COBS_H
#define COBS_H

#include <stdint.h>
#include <stddef.h>

/* Encode src[0..len-1] into dst. Returns encoded length (excluding delimiter).
 * dst must be at least len + (len/254) + 1 bytes.
 * Does NOT append the 0x00 delimiter — caller must add it. */
size_t cobs_encode(const uint8_t *src, size_t len, uint8_t *dst);

/* Decode src[0..len-1] into dst. Returns decoded length, or 0 on error.
 * src must NOT include the trailing 0x00 delimiter.
 * dst must be at least len bytes. */
size_t cobs_decode(const uint8_t *src, size_t len, uint8_t *dst);

#endif /* COBS_H */
