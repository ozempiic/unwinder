import os, subprocess, sys
from pathlib import Path

def find_msvc():
    vs_paths = [
        r"C:\Program Files (x86)\Microsoft Visual Studio\2019\Community",
        r"C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional",
        r"C:\Program Files (x86)\Microsoft Visual Studio\2019\Enterprise",
        r"C:\Program Files\Microsoft Visual Studio\2022\Community",
        r"C:\Program Files\Microsoft Visual Studio\2022\Professional",
        r"C:\Program Files\Microsoft Visual Studio\2022\Enterprise"
    ]
    
    for path in vs_paths:
        if os.path.exists(path):
            return path
    return None

def setup_msvc_environment():
    vs_path = find_msvc()
    if not vs_path:
        print("Error: Visual Studio not found. Please install Visual Studio with C++ development tools.")
        sys.exit(1)
    
    vcvarsall = Path(vs_path) / "VC" / "Auxiliary" / "Build" / "vcvarsall.bat"
    if not vcvarsall.exists():
        print(f"Error: vcvarsall.bat not found at expected location: {vcvarsall}")
        sys.exit(1)
    
    cmd = f'"{vcvarsall}" x64 && set'
    process = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, shell=True)
    stdout, stderr = process.communicate()
    
    env = os.environ.copy()
    for line in stdout.decode().splitlines():
        if '=' in line:
            key, value = line.split('=', 1)
            env[key] = value
    
    return env

def check_nasm():
    try:
        subprocess.run(['nasm', '-v'], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        return True
    except FileNotFoundError:
        print("Error: NASM not found. Please install NASM and add it to your PATH.")
        print("Download NASM from: https://www.nasm.us/")
        return False

def get_windows_sdk_version(sdk_root):
    include_path = os.path.join(sdk_root, "Include")
    if os.path.exists(include_path):
        versions = [d for d in os.listdir(include_path) 
                   if os.path.isdir(os.path.join(include_path, d))]
        if versions:
            return sorted(versions)[-1] 
    return None

def build_project():
    if not check_nasm():
        sys.exit(1)
    
    env = setup_msvc_environment()
    
    if not os.path.exists('build'):
        os.makedirs('build')
    
    sdk_root = r"C:\Program Files (x86)\Windows Kits\10"
    sdk_version = get_windows_sdk_version(sdk_root)
    if not sdk_version:
        print("Error: Could not find Windows SDK installation")
        sys.exit(1)
    
    um_path = os.path.join(sdk_root, "Include", sdk_version, "um")
    shared_path = os.path.join(sdk_root, "Include", sdk_version, "shared")
    
    if not os.path.exists(um_path) or not os.path.exists(shared_path):
        print(f"Error: Windows SDK paths not found:\n  {um_path}\n  {shared_path}")
        sys.exit(1)
    
    try:
        print("Compiling C code...")
        subprocess.run([
            'cl', '/c', '/Fo:build\\unwinder.obj', 
            '/Oy-',  
            'src\\unwinder.c',
            '/I', um_path,
            '/I', shared_path
        ], env=env, check=True)  
        
        print("Assembling ASM code...")
        subprocess.run([
            'nasm', '-f', 'win64', 
            'src\\unwinder_helpers.asm', 
            '-o', 'build\\unwinder_helpers.obj'
        ], check=True)
        
        subprocess.run([
            'nasm', '-f', 'win64', 
            'src\\test_push_nonvol.asm', 
            '-o', 'build\\test_push_nonvol.obj'
        ], check=True)
        
        # Add compilation for test_and_unwind.asm
        subprocess.run([
            'nasm', '-f', 'win64', 
            'src\\test_and_unwind.asm', 
            '-o', 'build\\test_and_unwind.obj'
        ], check=True)
        
        print("Linking...")
        lib_path = os.path.join(sdk_root, "Lib", sdk_version, "um", "x64")
        subprocess.run([
            'link', '/DLL', '/OUT:build\\unwinder.dll',
            '/LIBPATH:' + lib_path,
            'build\\unwinder.obj', 'build\\unwinder_helpers.obj',
            'build\\test_push_nonvol.obj', 'build\\test_and_unwind.obj',  # Add test_and_unwind.obj here
            'kernel32.lib', 'dbghelp.lib',
            '/DEF:src\\unwinder.def'
        ], env=env, check=True)  
        
        print("Build completed successfully!")
        
        print("\nVerifying exports...")
        subprocess.run([
            'dumpbin', '/EXPORTS', 'build\\unwinder.dll'
        ], env=env, check=True)
        
    except subprocess.CalledProcessError as e:
        print(f"Build failed with error: {e}")
        sys.exit(1)

if __name__ == "__main__":
    build_project()
