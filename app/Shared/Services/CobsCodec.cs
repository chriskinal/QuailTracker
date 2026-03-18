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

using System;

namespace QuailTracker.Shared.Services;

/// <summary>
/// COBS (Consistent Overhead Byte Stuffing) encoder/decoder.
/// Encodes data so no zero bytes appear, using 0x00 as frame delimiter.
/// </summary>
public static class CobsCodec
{
    /// <summary>
    /// Encode data using COBS. Returns encoded bytes (without trailing 0x00 delimiter).
    /// </summary>
    public static byte[] Encode(ReadOnlySpan<byte> data)
    {
        var output = new byte[data.Length + data.Length / 254 + 2];
        int readIdx = 0;
        int writeIdx = 1;
        int codeIdx = 0;
        byte code = 1;

        while (readIdx < data.Length)
        {
            if (data[readIdx] == 0x00)
            {
                output[codeIdx] = code;
                codeIdx = writeIdx++;
                code = 1;
            }
            else
            {
                output[writeIdx++] = data[readIdx];
                code++;
                if (code == 0xFF)
                {
                    output[codeIdx] = code;
                    codeIdx = writeIdx++;
                    code = 1;
                }
            }
            readIdx++;
        }
        output[codeIdx] = code;

        var result = new byte[writeIdx];
        Array.Copy(output, result, writeIdx);
        return result;
    }

    /// <summary>
    /// Decode COBS-encoded data. Input must NOT include the trailing 0x00 delimiter.
    /// Returns decoded bytes, or empty array on error.
    /// </summary>
    public static byte[] Decode(ReadOnlySpan<byte> data)
    {
        var output = new byte[data.Length];
        int readIdx = 0;
        int writeIdx = 0;

        while (readIdx < data.Length)
        {
            byte code = data[readIdx++];
            if (code == 0) return Array.Empty<byte>();

            for (int i = 1; i < code; i++)
            {
                if (readIdx >= data.Length) return Array.Empty<byte>();
                output[writeIdx++] = data[readIdx++];
            }
            if (code < 0xFF && readIdx < data.Length)
            {
                output[writeIdx++] = 0x00;
            }
        }

        // Remove trailing zero from last group
        if (writeIdx > 0) writeIdx--;

        var result = new byte[writeIdx];
        Array.Copy(output, result, writeIdx);
        return result;
    }
}
