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
            OPBFile.LogHandler = new LogHandler((s) => Console.Write(s));

            using (var opb = new OPBFile("doom.opb")) {
                var cmds1 = opb.ReadToEnd();
                opb.Reset();
                var cmds2 = opb.ReadToEnd();

                for (int i = 0; i < Math.Max(cmds1.Count, cmds2.Count); i++) {
                    if (cmds1[i] != cmds2[i]) {
                        Console.WriteLine("moop");
                    }
                }
            }

            //var testFile = "doom.vgm";
            //var vgmCmds = VGM.Parse(testFile);
            //OPBFile.OplToFile(OPBFormat.Default, vgmCmds, Path.GetFileNameWithoutExtension(testFile) + ".opb");

            //var fileIn = "doom.opb";
            //var fileOut = "doom-out.opb";

            //var commands = OPBFile.FileToOpl(fileIn);
            //OPBFile.OplToFile(OPBFormat.Default, commands, fileOut);
            //var commands2 = OPBFile.FileToOpl(fileOut);

            //for (int i = 0; i < commands.Count; i++) {
            //    if (commands[i].Addr != commands2[i].Addr || commands[i].Data != commands2[i].Data || Math.Abs(commands[i].Time.TotalMilliseconds - commands2[i].Time.TotalMilliseconds) > 0.00001) {
            //        throw new Exception();
            //    }
            //}
        }
    }
}
