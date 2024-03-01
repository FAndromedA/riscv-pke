#include "user_lib.h"
#include "util/types.h"
// to run this lab, we use "spike ./obj/riscv-pke ./bin/app_exec"
// and we do not implement close all the open virtual file of process in function exec
int main(int argc, char *argv[]) {
  printu("\n======== exec /bin/app_ls in app_exec ========\n");
  int ret = exec("/bin/app_ls");
  if (ret == -1)
    printu("exec failed!\n");

  exit(0);
  return 0;
}
