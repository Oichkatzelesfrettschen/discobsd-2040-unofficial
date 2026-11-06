/*
 * The PicoCalc keyboard controller as an I2C target at 0x1F.
 *
 * The register protocol follows the vendor firmware's receiveEvent() and
 * requestEvent() in picocalc_keyboard.ino (copied under docs/picocalc in the
 * notes repository): a write of a register number, optionally with bit 7 set
 * and a value byte, prepares a two-byte reply, and the next read returns it.
 * Register 0x01 answers (0, BIOSVERSION 0x16), register 0x09 dequeues one
 * (state, key) event from a 31-entry FIFO that drops new events when full
 * (conf_app.h FIFO_SIZE), 0x05 and 0x0A are the LCD and keyboard backlights,
 * 0x0B the battery, 0x04 the FIFO depth and lock flags, and 0x0E the
 * power-off countdown clamped to at least 6. Any other register answers
 * zeros. Register 0x08 resets the real controller after a delay, so the
 * model records the request in ResetRequests and does nothing else.
 *
 * Key injection happens through the monitor: PressKey, ReleaseKey, TypeKey,
 * TypeText, TypeLine and TypeCtrl queue the (state, key) events the firmware
 * would report. Shift, Caps Lock and the Alt symbol layer are applied by the
 * firmware before it queues a key, so TypeText queues the final character;
 * Ctrl is reported as key 0xA5 changing state and applied by the host, which
 * TypeCtrl reproduces. Hold-repeat events (state 2) are not generated, and
 * the 16 ms key-scan period is not modeled: an event is visible to the next
 * FIFO read.
 */

using System;
using System.Collections.Generic;
using Antmicro.Renode.Core;
using Antmicro.Renode.Logging;

namespace Antmicro.Renode.Peripherals.I2C
{
    public class PicoCalcKeyboard : II2CPeripheral
    {
        public const byte KeyEnter = 0x0a;
        public const byte KeyBackspace = 0x08;
        public const byte KeyTab = 0x09;
        public const byte KeyEscape = 0xb1;
        public const byte KeyCtrl = 0xa5;
        public const byte StatePressed = 1;
        public const byte StateHold = 2;
        public const byte StateReleased = 3;
        public const int FifoSize = 31;

        private const byte RegVersion = 0x01;
        private const byte RegKeyStatus = 0x04;
        private const byte RegBacklight = 0x05;
        private const byte RegReset = 0x08;
        private const byte RegFifo = 0x09;
        private const byte RegKeyBacklight = 0x0a;
        private const byte RegBattery = 0x0b;
        private const byte RegPowerOff = 0x0e;
        private const byte BiosVersion = 0x16;

        public PicoCalcKeyboard()
        {
            Reset();
        }

        public void Reset()
        {
            lock(sync)
            {
                fifo.Clear();
                reply[0] = 0;
                reply[1] = 0;
                readIndex = 0;
                backlight = 0x80;
                keyBacklight = 0;
                batteryPercent = 100;
                charging = false;
                powerOff = 0;
                Dropped = 0;
                Delivered = 0;
                Queued = 0;
                ResetRequests = 0;
                Transactions = 0;
            }
        }

        public void Write(byte[] data)
        {
            if(data == null || data.Length == 0)
            {
                return;
            }
            lock(sync)
            {
                Transactions++;
                var isWrite = (data[0] & 0x80) != 0;
                var reg = (byte)(data[0] & 0x7f);
                var value = data.Length > 1 ? data[1] : (byte)0;
                reply[0] = 0;
                reply[1] = 0;
                readIndex = 0;
                switch(reg)
                {
                case RegVersion:
                    reply[1] = BiosVersion;
                    break;
                case RegFifo:
                    if(fifo.Count > 0)
                    {
                        var ev = fifo.Dequeue();
                        reply[0] = ev.Item1;
                        reply[1] = ev.Item2;
                        Delivered++;
                    }
                    break;
                case RegKeyStatus:
                    reply[0] = (byte)fifo.Count;
                    break;
                case RegBacklight:
                    if(isWrite)
                    {
                        backlight = value;
                    }
                    reply[0] = reg;
                    reply[1] = backlight;
                    break;
                case RegKeyBacklight:
                    if(isWrite)
                    {
                        keyBacklight = value;
                    }
                    reply[0] = reg;
                    reply[1] = keyBacklight;
                    break;
                case RegBattery:
                    reply[0] = reg;
                    reply[1] = (byte)(batteryPercent | (charging ? 0x80 : 0));
                    break;
                case RegPowerOff:
                    if(isWrite)
                    {
                        powerOff = value < 6 ? (byte)6 : value;
                    }
                    reply[0] = reg;
                    reply[1] = 1;
                    break;
                case RegReset:
                    ResetRequests++;
                    this.Log(LogLevel.Warning, "picocalc kbd: reset register written; the model ignores it");
                    break;
                default:
                    break;
                }
            }
        }

        public byte[] Read(int count = 1)
        {
            lock(sync)
            {
                var result = new byte[count];
                for(var i = 0; i < count; i++)
                {
                    result[i] = reply[readIndex % 2];
                    readIndex++;
                }
                return result;
            }
        }

        public void FinishTransmission()
        {
            lock(sync)
            {
                readIndex = 0;
            }
        }

        /* Monitor interface. */

        public long Dropped { get; private set; }
        public long Delivered { get; private set; }
        public long Queued { get; private set; }
        public long ResetRequests { get; private set; }
        public long Transactions { get; private set; }

        public int Pending { get { lock(sync) { return fifo.Count; } } }
        public int BacklightLevel { get { lock(sync) { return backlight; } } }
        public int KeyBacklightLevel { get { lock(sync) { return keyBacklight; } } }
        public int PowerOffCountdown { get { lock(sync) { return powerOff; } } }

        public void SetBattery(uint percent, bool isCharging)
        {
            lock(sync)
            {
                batteryPercent = (byte)Math.Min(percent, 100u);
                charging = isCharging;
            }
        }

        public void PressKey(byte key)
        {
            Enqueue(StatePressed, key);
        }

        public void ReleaseKey(byte key)
        {
            Enqueue(StateReleased, key);
        }

        public void TypeKey(byte key)
        {
            lock(sync)
            {
                Enqueue(StatePressed, key);
                Enqueue(StateReleased, key);
            }
        }

        /* Type each character of text; a backslash-n pair counts as Enter. */
        public void TypeText(string text)
        {
            lock(sync)
            {
                for(var i = 0; i < text.Length; i++)
                {
                    if(text[i] == '\\' && i + 1 < text.Length && text[i + 1] == 'n')
                    {
                        TypeKey(KeyEnter);
                        i++;
                    }
                    else
                    {
                        TypeKey((byte)text[i]);
                    }
                }
            }
        }

        public void TypeLine(string text)
        {
            lock(sync)
            {
                TypeText(text);
                TypeKey(KeyEnter);
            }
        }

        /* Ctrl down, the letter down and up, Ctrl up. */
        public void TypeCtrl(char letter)
        {
            lock(sync)
            {
                Enqueue(StatePressed, KeyCtrl);
                TypeKey((byte)letter);
                Enqueue(StateReleased, KeyCtrl);
            }
        }

        private void Enqueue(byte state, byte key)
        {
            lock(sync)
            {
                if(fifo.Count >= FifoSize)
                {
                    Dropped++;
                    return;
                }
                fifo.Enqueue(Tuple.Create(state, key));
                Queued++;
            }
        }

        private readonly object sync = new object();
        private readonly Queue<Tuple<byte, byte>> fifo = new Queue<Tuple<byte, byte>>();
        private readonly byte[] reply = new byte[2];
        private int readIndex;
        private byte backlight, keyBacklight, batteryPercent, powerOff;
        private bool charging;
    }
}
