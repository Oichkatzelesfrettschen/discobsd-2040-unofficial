/*
 * The PicoCalc panel: an ST7365P on SPI1 that keeps the 320x480 graphics RAM
 * the driver writes and renders the 320x320 visible window to a PPM file on
 * request.
 *
 * The model is an ISPIPeripheral registered under the RP2040 PL022 (SPI1 in
 * picocalc.repl), which hands it each frame of SSPDR as a byte once
 * patch 0008 of tools/renode/patches is applied; Transmit() latches the
 * byte as a command or as data according to the D/C level. Three GPIO
 * inputs carry the rest of the panel's pins: input 0 is chip select (GP13),
 * input 1 is D/C (GP14) and input 2 is reset (GP15). A byte received while
 * chip select is high is counted and dropped. ST7365P_SPEC_V1.0 page 54 has
 * the controller latch D/C with the eighth clock; the driver changes D/C
 * only while the PL022 is idle, so the level at the SSPDR write is the level
 * at the eighth clock.
 *
 * What it decodes: CASET, RASET, RAMWR, RAMWRC, VSCRDEF, VSCSAD, MADCTL,
 * COLMOD, SLPIN, SLPOUT, INVON, INVOFF, DISPON, DISPOFF and SWRESET. The
 * gamma, power, VCOM and timing commands st7365p.c sends during bring-up
 * have no effect on the pixel image and are counted but not interpreted.
 * The image shown is the graphics RAM as written: MADCTL MX and BGR and the
 * INVON polarity compensate the board's panel wiring and are recorded in
 * the dump header instead of being applied, and MADCTL MV (row/column
 * exchange) is not modeled. Nothing here establishes the panel's colors,
 * contrast, timing or backlight; those are board observations.
 */

using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
using Antmicro.Renode.Core;
using Antmicro.Renode.Logging;

namespace Antmicro.Renode.Peripherals.SPI
{
    public class ST7365P : ISPIPeripheral, IGPIOReceiver
    {
        public const int Width = 320;
        public const int VisibleLines = 320;
        public const int GramLines = 480;

        private const int PinCs = 0;
        private const int PinDc = 1;
        private const int PinReset = 2;

        public ST7365P()
        {
            gram = new byte[Width * GramLines * 3];
            commandCounts = new Dictionary<byte, long>();
            Reset();
        }

        public void Reset()
        {
            lock(sync)
            {
                Array.Clear(gram, 0, gram.Length);
                commandCounts.Clear();
                cs = true;
                dc = true;
                command = 0;
                paramCount = 0;
                colStart = 0;
                colEnd = Width - 1;
                rowStart = 0;
                rowEnd = GramLines - 1;
                curX = 0;
                curY = 0;
                topFixed = 0;
                scrollArea = GramLines;
                bottomFixed = 0;
                scrollStart = 0;
                madctl = 0;
                colmod = 0x66;
                displayOn = false;
                sleeping = true;
                inverted = false;
                pixelPhase = 0;
                pixelHi = 0;
                pixelBytes = new byte[3];
                PixelsWritten = 0;
                FramesRequested = 0;
                DroppedWithoutSelect = 0;
                OutOfWindowPixels = 0;
                UnknownCommands = 0;
                BytesReceived = 0;
                Generation = 0;
            }
        }

        public void OnGPIO(int number, bool value)
        {
            lock(sync)
            {
                switch(number)
                {
                case PinCs:
                    cs = value;
                    break;
                case PinDc:
                    dc = value;
                    break;
                case PinReset:
                    if(!value)
                    {
                        HardwareReset();
                    }
                    break;
                }
            }
        }

        public byte Transmit(byte data)
        {
            lock(sync)
            {
                BytesReceived++;
                if(cs)
                {
                    DroppedWithoutSelect++;
                    return 0;
                }
                if(traceLimit > 0)
                {
                    trace.AppendFormat("{0}{1:x2} ", dc ? 'd' : 'c', data);
                    traceLimit--;
                }
                Byte(data, dc);
            }
            return 0;
        }

        public void FinishTransmission()
        {
        }

        /* Monitor interface. */

        public long PixelsWritten { get; private set; }
        public long BytesReceived { get; private set; }
        public long UnknownCommands { get; private set; }
        public long OutOfWindowPixels { get; private set; }
        public long FramesRequested { get; private set; }
        public long DroppedWithoutSelect { get; private set; }

        /* Counts every change of the graphics RAM or its scroll origin. */
        public long Generation { get; private set; }

        public bool DisplayOn { get { lock(sync) { return displayOn; } } }
        public int ScrollStart { get { lock(sync) { return scrollStart; } } }

        /* The first bytes after a reset or TraceBytes call: c for command, d for data. */
        public string Trace()
        {
            lock(sync)
            {
                return trace.ToString();
            }
        }

        public void TraceBytes(int count)
        {
            lock(sync)
            {
                trace.Clear();
                traceLimit = count;
            }
        }

        public string Status()
        {
            lock(sync)
            {
                var sb = new StringBuilder();
                sb.AppendFormat("display {0}, sleep {1}, invert {2}, madctl 0x{3:x2}, colmod 0x{4:x2}",
                    displayOn ? "on" : "off", sleeping ? "in" : "out", inverted ? "on" : "off", madctl, colmod);
                sb.AppendFormat(", scroll top {0} area {1} bottom {2} start {3}",
                    topFixed, scrollArea, bottomFixed, scrollStart);
                sb.AppendFormat(", {0} bytes, {1} pixels, {2} outside the window, {3} unknown commands, {4} without chip select",
                    BytesReceived, PixelsWritten, OutOfWindowPixels, UnknownCommands, DroppedWithoutSelect);
                return sb.ToString();
            }
        }

        /* Number of times the command was received since reset. */
        public long CommandCount(byte cmd)
        {
            lock(sync)
            {
                long n;
                return commandCounts.TryGetValue(cmd, out n) ? n : 0;
            }
        }

        /*
         * Write the visible 320x320 image as binary PPM (P6). The first line
         * after the magic number is a comment carrying the registers that
         * decide how a real panel would present the same RAM.
         */
        public string DumpPpm(string path)
        {
            byte[] image;
            string header;
            lock(sync)
            {
                FramesRequested++;
                image = new byte[Width * VisibleLines * 3];
                for(var y = 0; y < VisibleLines; y++)
                {
                    Buffer.BlockCopy(gram, SourceLine(y) * Width * 3, image, y * Width * 3, Width * 3);
                }
                header = String.Format("# st7365p display {0} madctl 0x{1:x2} colmod 0x{2:x2} invon {3} vscsad {4} generation {5}\n",
                    displayOn ? "on" : "off", madctl, colmod, inverted ? 1 : 0, scrollStart, Generation);
            }
            WritePpm(path, header, Width, VisibleLines, image);
            return path;
        }

        /* Write the whole 320x480 graphics RAM, unscrolled. */
        public string DumpGramPpm(string path)
        {
            byte[] copy;
            lock(sync)
            {
                copy = (byte[])gram.Clone();
            }
            WritePpm(path, "# st7365p graphics RAM\n", Width, GramLines, copy);
            return path;
        }

        private static void WritePpm(string path, string comment, int w, int h, byte[] rgb)
        {
            var tmp = path + ".part";
            using(var f = new FileStream(tmp, FileMode.Create, FileAccess.Write))
            {
                var head = Encoding.ASCII.GetBytes("P6\n" + comment + w + " " + h + "\n255\n");
                f.Write(head, 0, head.Length);
                f.Write(rgb, 0, rgb.Length);
            }
            File.Move(tmp, path, true);
        }

        /* The RAM line the panel shows on visible line y (datasheet VSCRDEF, VSCSAD). */
        private int SourceLine(int y)
        {
            if(y < topFixed)
            {
                return y;
            }
            if(y < topFixed + scrollArea)
            {
                var offset = scrollStart - topFixed;
                if(offset < 0 || offset >= scrollArea)
                {
                    offset = 0;
                }
                return topFixed + ((y - topFixed + offset) % scrollArea);
            }
            return y;
        }

        private void HardwareReset()
        {
            var keep = (byte[])gram.Clone();
            var pixels = PixelsWritten;
            var bytes = BytesReceived;
            Reset();
            Buffer.BlockCopy(keep, 0, gram, 0, gram.Length);
            PixelsWritten = pixels;
            BytesReceived = bytes;
            cs = true;
        }

        private void Byte(byte b, bool data)
        {
            if(!data)
            {
                StartCommand(b);
                return;
            }
            if(command == 0x2c || command == 0x3c)
            {
                PixelByte(b);
                return;
            }
            Parameter(b);
        }

        private void StartCommand(byte cmd)
        {
            command = cmd;
            paramCount = 0;
            long n;
            commandCounts.TryGetValue(cmd, out n);
            commandCounts[cmd] = n + 1;
            pixelPhase = 0;

            switch(cmd)
            {
            case 0x01:  /* SWRESET */
                madctl = 0;
                colmod = 0x66;
                topFixed = 0;
                scrollArea = GramLines;
                bottomFixed = 0;
                scrollStart = 0;
                inverted = false;
                displayOn = false;
                sleeping = true;
                Generation++;
                break;
            case 0x10: sleeping = true; break;
            case 0x11: sleeping = false; break;
            case 0x20: inverted = false; break;
            case 0x21: inverted = true; break;
            case 0x28: displayOn = false; break;
            case 0x29: displayOn = true; break;
            case 0x2c:  /* RAMWR restarts at the window origin; RAMWRC continues. */
                curX = colStart;
                curY = rowStart;
                break;
            case 0x3c:
                break;
            case 0x2a: case 0x2b: case 0x33: case 0x36: case 0x37: case 0x3a:
                break;
            default:
                if(!IsConfigurationCommand(cmd))
                {
                    UnknownCommands++;
                    this.Log(LogLevel.Warning, "st7365p: command 0x{0:x2} is not modeled", cmd);
                }
                break;
            }
        }

        /* Bring-up registers that change the glass, not the pixel image. */
        private static bool IsConfigurationCommand(byte cmd)
        {
            switch(cmd)
            {
            case 0xb0: case 0xb1: case 0xb4: case 0xb6: case 0xb7:
            case 0xc0: case 0xc1: case 0xc5:
            case 0xe0: case 0xe1: case 0xf7:
                return true;
            default:
                return false;
            }
        }

        private int[] pending = new int[6];

        private void Parameter(byte b)
        {
            if(paramCount < pending.Length)
            {
                pending[paramCount] = b;
            }
            paramCount++;
            switch(command)
            {
            case 0x2a:
                if(paramCount == 4)
                {
                    colStart = (pending[0] << 8) | pending[1];
                    colEnd = (pending[2] << 8) | pending[3];
                }
                break;
            case 0x2b:
                if(paramCount == 4)
                {
                    rowStart = (pending[0] << 8) | pending[1];
                    rowEnd = (pending[2] << 8) | pending[3];
                }
                break;
            case 0x33:
                if(paramCount == 6)
                {
                    var tfa = (pending[0] << 8) | pending[1];
                    var vsa = (pending[2] << 8) | pending[3];
                    var bfa = (pending[4] << 8) | pending[5];
                    if(tfa + vsa + bfa == GramLines)
                    {
                        topFixed = tfa;
                        scrollArea = vsa;
                        bottomFixed = bfa;
                        Generation++;
                    }
                    else
                    {
                        this.Log(LogLevel.Warning, "st7365p: VSCRDEF {0}+{1}+{2} does not sum to {3}; ignored",
                            tfa, vsa, bfa, GramLines);
                    }
                }
                break;
            case 0x36:
                madctl = b;
                if((b & 0x20) != 0)
                {
                    this.Log(LogLevel.Warning, "st7365p: MADCTL MV is not modeled");
                }
                break;
            case 0x37:
                if(paramCount == 2)
                {
                    scrollStart = (pending[0] << 8) | pending[1];
                    Generation++;
                }
                break;
            case 0x3a:
                colmod = b;
                break;
            }
        }

        /*
         * One pixel is three bytes at COLMOD 0x66 (the upper six bits of each
         * channel) or 0x77 (eight bits), and two bytes at 0x55 (RGB565).
         */
        private void PixelByte(byte b)
        {
            var wide = (colmod & 0x07) == 0x05;
            if(wide)
            {
                if(pixelPhase == 0)
                {
                    pixelHi = b;
                    pixelPhase = 1;
                    return;
                }
                var v = (pixelHi << 8) | b;
                var r = (v >> 11) & 0x1f;
                var g = (v >> 5) & 0x3f;
                var bl = v & 0x1f;
                PutPixel((byte)((r << 3) | (r >> 2)), (byte)((g << 2) | (g >> 4)), (byte)((bl << 3) | (bl >> 2)));
                pixelPhase = 0;
                return;
            }
            pixelBytes[pixelPhase++] = b;
            if(pixelPhase == 3)
            {
                pixelPhase = 0;
                if((colmod & 0x07) == 0x06)
                {
                    PutPixel(Expand6(pixelBytes[0]), Expand6(pixelBytes[1]), Expand6(pixelBytes[2]));
                }
                else
                {
                    PutPixel(pixelBytes[0], pixelBytes[1], pixelBytes[2]);
                }
            }
        }

        private static byte Expand6(byte v)
        {
            var six = (v >> 2) & 0x3f;
            return (byte)((six << 2) | (six >> 4));
        }

        private void PutPixel(byte r, byte g, byte b)
        {
            PixelsWritten++;
            if(curX >= 0 && curX < Width && curY >= 0 && curY < GramLines)
            {
                var o = (curY * Width + curX) * 3;
                gram[o] = r;
                gram[o + 1] = g;
                gram[o + 2] = b;
                Generation++;
            }
            else
            {
                OutOfWindowPixels++;
            }
            curX++;
            if(curX > colEnd)
            {
                curX = colStart;
                curY++;
                if(curY > rowEnd)
                {
                    curY = rowStart;
                }
            }
        }

        private readonly object sync = new object();
        private readonly StringBuilder trace = new StringBuilder();
        private int traceLimit;
        private readonly byte[] gram;
        private readonly Dictionary<byte, long> commandCounts;
        private bool cs, dc;
        private byte command;
        private int paramCount;
        private int colStart, colEnd, rowStart, rowEnd, curX, curY;
        private int topFixed, scrollArea, bottomFixed, scrollStart;
        private byte madctl, colmod;
        private bool displayOn, sleeping, inverted;
        private int pixelPhase;
        private byte pixelHi;
        private byte[] pixelBytes;
    }
}
