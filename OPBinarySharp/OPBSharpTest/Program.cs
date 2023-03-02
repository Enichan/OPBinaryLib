using OPBinarySharp;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace OPBSharpTest {
    class Program {
        static void Main() {
            OPB.LogHandler = new LogHandler((s) => Console.Write(s));

            var testFile = "doom.vgm";
            var vgmCmds = VGM.Parse(testFile);
            OPB.OplToFile(OPBFormat.Default, vgmCmds, Path.GetFileNameWithoutExtension(testFile) + ".opb");

            //var fileIn = "doom.opb";
            //var fileOut = "doom-out.opb";

            //var commands = OPB.FileToOpl(fileIn);
            //OPB.OplToFile(OPBFormat.Default, commands, fileOut);
            //var commands2 = OPB.FileToOpl(fileOut);

            //for (int i = 0; i < commands.Count; i++) {
            //    if (commands[i].Addr != commands2[i].Addr || commands[i].Data != commands2[i].Data || Math.Abs(commands[i].Time.TotalMilliseconds - commands2[i].Time.TotalMilliseconds) > 0.00001) {
            //        throw new Exception();
            //    }
            //}
        }
    }
}
