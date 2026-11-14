/* Original model code is under the repository's BSD 3-Clause license. */
using System;
using System.Collections.Generic;
using System.IO;
using Antmicro.Renode.Core;

namespace Antmicro.Renode.Peripherals.SPI
{
    // Single-sector SPI commands used by sdspi.c. File-backed capacity does
    // not allocate card-sized memory. No wear, analog or power-loss model.
    public class FileSD : ISPIPeripheral, IGPIOReceiver, IDisposable
    {
        public void LoadImage(string path, bool readOnly = false)
        {
            var next = new FileStream(path, FileMode.Open,
                readOnly ? FileAccess.Read : FileAccess.ReadWrite, FileShare.Read);
            if(next.Length < 524288 || next.Length > 34359738368L || next.Length % 524288 != 0)
            {
                next.Dispose();
                throw new ArgumentException("SD image must be 512 KiB aligned, 512 KiB through 32 GiB");
            }
            if(image != null) image.Dispose();
            image = next;
            ReadOnly = readOnly;
            Sectors = next.Length / 512;
            highCapacity = next.Length > 2147483648L;
            Reset();
        }

        public void Dispose()
        {
            if(image != null) image.Dispose();
            image = null;
            Sectors = 0;
            Reset();
        }

        public long Sectors { get; private set; }
        public long Reads { get; private set; }
        public long Writes { get; private set; }
        public long Rejected { get; private set; }
        public bool ReadOnly { get; private set; }

        public void Reset()
        {
            selected = false;
            idle = true;
            app = false;
            count = 0;
            writeIndex = -1;
            response.Clear();
            Reads = Writes = Rejected = 0;
        }

        public void OnGPIO(int number, bool value)
        {
            if(number != 0) throw new ArgumentException("FileSD input 0 is active-low CS");
            selected = !value;
            if(!selected)
            {
                count = 0;
                writeIndex = -1;
                response.Clear();
            }
        }

        public byte Transmit(byte value)
        {
            if(!selected || image == null) return 0xff;
            if(response.Count != 0) return response.Dequeue();
            if(writeIndex != -1)
            {
                if(writeIndex == -2)
                {
                    if(value == 0xfe) writeIndex = 0;
                    return 0xff;
                }
                if(writeIndex < 512) sector[writeIndex] = value;
                writeIndex++;
                if(writeIndex == 514)
                {
                    writeIndex = -1;
                    image.Position = target * 512;
                    image.Write(sector, 0, 512);
                    image.Flush();
                    Writes++;
                    response.Enqueue(0x05);
                    response.Enqueue(0x00);
                    response.Enqueue(0xff);
                }
                return 0xff;
            }
            if(count == 0 && (value & 0xc0) != 0x40) return 0xff;
            command[count++] = value;
            if(count == 6)
            {
                count = 0;
                Execute();
            }
            return 0xff;
        }

        public void FinishTransmission() { }

        private void Execute()
        {
            var cmd = command[0] & 0x3f;
            uint arg = ((uint)command[1] << 24) | ((uint)command[2] << 16)
                | ((uint)command[3] << 8) | command[4];
            var wasApp = app;
            app = false;
            byte r1 = idle ? (byte)1 : (byte)0;
            if((cmd == 0 && command[5] != 0x95) || (cmd == 8 && command[5] != 0x87))
            {
                response.Enqueue((byte)(r1 | 8));
                Rejected++;
                return;
            }
            switch(cmd)
            {
            case 0:
                idle = true;
                response.Enqueue(1);
                break;
            case 8:
                response.Enqueue(r1);
                response.Enqueue(0); response.Enqueue(0);
                response.Enqueue(1); response.Enqueue(0xaa);
                break;
            case 55:
                app = true;
                response.Enqueue(r1);
                break;
            case 41:
                if(!wasApp) { response.Enqueue((byte)(r1 | 4)); Rejected++; break; }
                idle = false;
                response.Enqueue(0);
                break;
            case 58:
                response.Enqueue(r1);
                response.Enqueue((byte)((idle ? 0 : 0x80) | (highCapacity ? 0x40 : 0)));
                response.Enqueue(0xff); response.Enqueue(0x80); response.Enqueue(0);
                break;
            case 16:
                response.Enqueue((byte)(r1 | (arg == 512 ? 0 : 0x40)));
                break;
            case 9:
                response.Enqueue(r1);
                if(!idle) SendData(Csd());
                break;
            case 17:
            case 24:
                target = highCapacity ? arg : arg / 512;
                if(idle || (!highCapacity && arg % 512 != 0) || target >= Sectors)
                {
                    response.Enqueue((byte)(r1 | 0x20)); Rejected++; break;
                }
                if(cmd == 24 && ReadOnly)
                {
                    response.Enqueue(4); Rejected++; break;
                }
                response.Enqueue(0);
                if(cmd == 24) { writeIndex = -2; break; }
                image.Position = target * 512;
                var have = 0;
                while(have < 512)
                {
                    var n = image.Read(sector, have, 512 - have);
                    if(n == 0) throw new EndOfStreamException("SD backing image truncated");
                    have += n;
                }
                SendData(sector);
                Reads++;
                break;
            default:
                response.Enqueue((byte)(r1 | 4)); Rejected++; break;
            }
        }

        private byte[] Csd()
        {
            var data = new byte[16];
            data[5] = 9;
            if(highCapacity)
            {
                var size = (uint)(Sectors / 1024 - 1);
                data[0] = 0x40;
                data[7] = (byte)((size >> 16) & 0x3f);
                data[8] = (byte)(size >> 8); data[9] = (byte)size;
            }
            else
            {
                var mult = 0;
                while((Sectors >> (mult + 2)) > 4096) mult++;
                var size = (uint)(Sectors >> (mult + 2)) - 1;
                data[6] = (byte)(size >> 10);
                data[7] = (byte)(size >> 2);
                data[8] = (byte)((size & 3) << 6);
                data[9] = (byte)(mult >> 1);
                data[10] = (byte)((mult & 1) << 7);
            }
            return data;
        }

        private void SendData(byte[] data)
        {
            response.Enqueue(0xfe);
            foreach(var b in data) response.Enqueue(b);
            response.Enqueue(0xff); response.Enqueue(0xff);
        }

        private FileStream image;
        private readonly Queue<byte> response = new Queue<byte>();
        private readonly byte[] command = new byte[6];
        private readonly byte[] sector = new byte[512];
        private bool selected, idle, app, highCapacity;
        private int count, writeIndex = -1;
        private long target;
    }
}
