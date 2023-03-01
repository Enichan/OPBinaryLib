using OPBinarySharp;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace OPBSharpTest {
    class Program {
        static void Main() {
            var fileIn = "test.opb";
            var fileOut = "test-out.opb";

            var commands = OPB.FileToOpl(fileIn);
            OPB.OplToFile(OPBFormat.Default, commands, fileOut);
            var commands2 = OPB.FileToOpl(fileOut);

            for (int i = 0; i < commands.Count; i++) {
                if (commands[i].Addr != commands2[i].Addr || commands[i].Data != commands2[i].Data || Math.Abs(commands[i].Time.TotalMilliseconds - commands2[i].Time.TotalMilliseconds) != 0) {
                    throw new Exception();
                }
            }
        }
    }
}
