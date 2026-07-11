from shutil import copyfile
import os

Import("env", "projenv")

def save_hex(*args, **kwargs):
    print("Copying hex output to project directory...")
    target = str(kwargs['target'][0])
    
    # Check if LARGEPSRAM is defined in build flags
    cppdefines = env.get("CPPDEFINES", [])
    # CPPDEFINES may contain tuples like ('LARGEPSRAM', None) or strings like "LARGEPSRAM=1"
    is_large_psram = any(
        defn == "LARGEPSRAM" or (isinstance(defn, tuple) and defn[0] == "LARGEPSRAM")
        for defn in cppdefines
    )
    
    dest_dir = "release/32MB_PSRAM_ONLY" if is_large_psram else "release"
    os.makedirs(dest_dir, exist_ok=True)  # ensure directory exists
    
    dest_path = os.path.join(dest_dir, os.path.basename(target))
    copyfile(target, dest_path)
    print(f"Done. Copied to {dest_path}")

env.AddPostAction("$BUILD_DIR/${PROGNAME}.hex", save_hex)
