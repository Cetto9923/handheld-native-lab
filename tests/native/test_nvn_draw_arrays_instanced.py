"""Exercise the generated public ABI wrapper at its recorder boundary.

NVN takes (primitive, first, count, baseInstance, instanceCount). The recorder
takes instanceCount before baseInstance. Keep these values distinct: the LUT
draw uses baseInstance=0, instanceCount=32. This test does not execute a GPU.
"""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest


class DrawArraysInstancedABI(unittest.TestCase):
    def test_instance_count_and_base_instance_are_not_swapped(self):
        root = Path(__file__).resolve().parents[2]
        source = (root / "src/graphics/nvn_device/nvn_command_buffer.generated.c").read_text()
        match = re.search(r"void nvnCommandBufferDrawArraysInstanced\([^}]+\n}", source)
        self.assertIsNotNone(match)
        harness = r'''
#include <stdint.h>
#include <stdio.h>
typedef struct { int unused; } NVNcommandBuffer;
typedef int NVNdrawPrimitive;
static int actual[5];
#define NVN_VOID_RETURN() return
static void nvn_command_buffer_rec_draw(void *cb, int primitive, int first,
        int count, int instances, int base_instance, uint64_t indirect) {
    (void)cb; (void)indirect;
    actual[0]=primitive; actual[1]=first; actual[2]=count;
    actual[3]=instances; actual[4]=base_instance;
}
''' + match.group(0) + r'''
int main(void) {
    NVNcommandBuffer cb={0};
    const int cases[][2]={{0,32},{17,5},{3,0}};
    for (unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);i++) {
        int base=cases[i][0], instances=cases[i][1];
        nvnCommandBufferDrawArraysInstanced(&cb,7,3,42,base,instances);
        if(actual[0]!=7 || actual[1]!=3 || actual[2]!=42 ||
           actual[3]!=instances || actual[4]!=base) {
            fprintf(stderr,"base=%d instances=%d became base=%d instances=%d\n",
                    base,instances,actual[4],actual[3]);
            return 1;
        }
    }
    return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix="ffrs-instanced-abi-") as temp:
            c = Path(temp) / "wrapper.c"
            exe = Path(temp) / "wrapper"
            c.write_text(harness)
            subprocess.run(["clang", "-std=c11", "-Wall", "-Wextra", "-Werror", str(c), "-o", str(exe)], check=True)
            result = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
