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
using System.Buffers.Binary;
using Google.Protobuf;

namespace QuailTracker.Shared.Services;

/// <summary>
/// Topic IDs for the BLE protobuf protocol. Must match firmware ble_proto.h.
/// </summary>
public enum BleProtoTopic : byte
{
    // Device → App (push)
    Status          = 0x01,
    Detection       = 0x02,
    AudioLevel      = 0x03,
    RecordingState  = 0x04,
    GpsFix          = 0x05,
    Log             = 0x06,

    // App → Device (command)
    Command         = 0x10,
    SetConfig       = 0x11,
    Subscribe       = 0x12,
    Unsubscribe     = 0x13,

    // Device → App (response)
    CommandAck      = 0x20,
    ConfigDump      = 0x21,

    // Bidirectional
    Ping            = 0x30,
    Pong            = 0x31,

    // OTA
    OtaBegin        = 0x40,
    OtaData         = 0x41,
    OtaEnd          = 0x42,
    OtaCommit       = 0x43,
    OtaAbort        = 0x44,
    OtaStatus       = 0x45,
    OtaRollback     = 0x46,
}

/// <summary>
/// Builds and parses COBS-framed protobuf messages for BLE transport.
///
/// Wire format: [COBS-encoded(4-byte header + protobuf payload)] [0x00]
/// Header: [topic:1] [flags:1] [seq_lo:1] [seq_hi:1]
/// </summary>
public class BleProtoFrame
{
    private const int HeaderSize = 4;
    private ushort _txSeq;

    /// <summary>
    /// Encode a protobuf message into a COBS-framed byte array ready to transmit.
    /// Includes the trailing 0x00 delimiter.
    /// </summary>
    public byte[] Encode(BleProtoTopic topic, IMessage message)
    {
        var pbBytes = message.ToByteArray();
        return EncodeRaw(topic, pbBytes);
    }

    /// <summary>
    /// Encode raw payload bytes into a COBS-framed byte array.
    /// </summary>
    public byte[] EncodeRaw(BleProtoTopic topic, ReadOnlySpan<byte> payload)
    {
        // Build raw frame: header + payload
        var raw = new byte[HeaderSize + payload.Length];
        raw[0] = (byte)topic;
        raw[1] = 0; // flags
        BinaryPrimitives.WriteUInt16LittleEndian(raw.AsSpan(2), _txSeq++);
        payload.CopyTo(raw.AsSpan(HeaderSize));

        // COBS encode + delimiter
        var encoded = CobsCodec.Encode(raw);
        var frame = new byte[encoded.Length + 1];
        Array.Copy(encoded, frame, encoded.Length);
        frame[^1] = 0x00; // delimiter
        return frame;
    }

    /// <summary>
    /// Encode a simple topic-only frame (no payload), e.g. Ping.
    /// </summary>
    public byte[] EncodeEmpty(BleProtoTopic topic)
    {
        return EncodeRaw(topic, ReadOnlySpan<byte>.Empty);
    }

    /// <summary>
    /// Decode a COBS frame (without trailing 0x00) into topic + protobuf payload.
    /// Returns false if the frame is invalid.
    /// </summary>
    public static bool TryDecode(ReadOnlySpan<byte> cobsFrame, out BleProtoTopic topic,
                                  out ushort sequence, out byte[] payload)
    {
        topic = 0;
        sequence = 0;
        payload = Array.Empty<byte>();

        var decoded = CobsCodec.Decode(cobsFrame);
        if (decoded.Length < HeaderSize)
            return false;

        topic = (BleProtoTopic)decoded[0];
        sequence = BinaryPrimitives.ReadUInt16LittleEndian(decoded.AsSpan(2));
        payload = new byte[decoded.Length - HeaderSize];
        Array.Copy(decoded, HeaderSize, payload, 0, payload.Length);
        return true;
    }
}

/// <summary>
/// Accumulates BLE characteristic data and extracts complete COBS frames
/// delimited by 0x00 bytes.
/// </summary>
public class BleFrameAssembler
{
    private readonly byte[] _buffer = new byte[512];
    private int _pos;

    /// <summary>
    /// Feed received BLE data. Calls onFrame for each complete frame found.
    /// </summary>
    public void Feed(ReadOnlySpan<byte> data, Action<ReadOnlySpan<byte>> onFrame)
    {
        foreach (var b in data)
        {
            if (b == 0x00)
            {
                if (_pos > 0)
                {
                    onFrame(_buffer.AsSpan(0, _pos));
                    _pos = 0;
                }
            }
            else
            {
                if (_pos < _buffer.Length)
                    _buffer[_pos++] = b;
                else
                    _pos = 0; // overflow — discard and resync
            }
        }
    }

    /// <summary>
    /// Reset assembler state (call on disconnect).
    /// </summary>
    public void Reset() => _pos = 0;
}
