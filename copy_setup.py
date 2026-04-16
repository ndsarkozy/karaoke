Import("env")
import shutil, os

src = os.path.join(env["PROJECT_INCLUDE_DIR"], "User_Setup.h")
dst = os.path.join(env["PROJECT_LIBDEPS_DIR"],
      env["PIOENV"], "TFT_eSPI", "User_Setup.h")

if os.path.isdir(os.path.dirname(dst)):
    shutil.copy(src, dst)
    print(">>> Copied User_Setup.h to TFT_eSPI")