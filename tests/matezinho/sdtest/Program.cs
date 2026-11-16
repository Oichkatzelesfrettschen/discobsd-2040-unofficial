/* Test-only interface seams; whole-machine Renode tests use real interfaces. */
using System;
using System.IO;
using System.Runtime.InteropServices;
using Antmicro.Renode.Peripherals.SPI;
namespace Antmicro.Renode.Core { public interface IGPIOReceiver { void OnGPIO(int n, bool v); } }
namespace Antmicro.Renode.Peripherals.SPI {
    public interface ISPIPeripheral { byte Transmit(byte b); void FinishTransmission(); void Reset(); }
}
class Program
{
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] delegate byte Xfer(byte b);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] delegate void Select(int n);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] delegate uint Micros();
    [StructLayout(LayoutKind.Sequential)] struct Ops { public Xfer xfer; public Select select, speed; public Micros micros; }
    [DllImport("sd_driver", CallingConvention=CallingConvention.Cdecl)]
    static extern int sd_driver_test(ref Ops ops, uint sectors, int readOnly);
    static void Main()
    {
        var folder=Path.Combine(Path.GetTempPath(),"mz-sd-"+Guid.NewGuid());
        Directory.CreateDirectory(folder);
        try
        {
            foreach(var size in new long[]{16777216,33554432,31999918080})
            {
                var path=Path.Combine(folder,size+".img");
                using(var file=File.Create(path))file.SetLength(size);
                using var card=new FileSD();
                card.LoadImage(path);
                uint now=0;
                var ops=new Ops { xfer=card.Transmit, select=n=>card.OnGPIO(0,n==0),
                    speed=n=>{}, micros=()=>now+=100 };
                var code=sd_driver_test(ref ops,(uint)(size/512),0);
                if(code!=0)throw new Exception("production sdspi.c failed at step "+code+" size "+size);
                card.LoadImage(path,true);
                code=sd_driver_test(ref ops,(uint)(size/512),1);
                if(code!=0)throw new Exception("read-only refusal failed at step "+code);
                // Calibration: remove the transport; the real driver must reject it.
                var dead=new Ops { xfer=b=>255,select=n=>{},speed=n=>{},micros=()=>now+=100 };
                if(sd_driver_test(ref dead,(uint)(size/512),0)==0)
                    throw new Exception("absent transport incorrectly passed");
                GC.KeepAlive(ops);
                Console.WriteLine("FileSD + production sdspi.c: "+size+" bytes, read/write last sector and read-only refusal passed");
            }
        }
        finally { Directory.Delete(folder,true); }
    }
}
