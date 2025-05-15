import ctypes
import os
import sys
from ctypes import *

class M128A(Structure):
    _fields_ = [
        ("Low", c_ulonglong),
        ("High", c_longlong)
    ]

class XMM_SAVE_AREA32(Structure):
    _fields_ = [
        ('ControlWord', c_ushort),
        ('StatusWord', c_ushort),
        ('TagWord', c_ubyte),
        ('Reserved1', c_ubyte),
        ('ErrorOpcode', c_ushort),
        ('ErrorOffset', c_ulong),
        ('ErrorSelector', c_ushort),
        ('Reserved2', c_ushort),
        ('DataOffset', c_ulong),
        ('DataSelector', c_ushort),
        ('Reserved3', c_ushort),
        ('MxCsr', c_ulong),
        ('MxCsr_Mask', c_ulong),
        ('FloatRegisters', M128A * 8),
        ('XmmRegisters', M128A * 16),
        ('Reserved4', c_byte * 96)
    ]

class CONTEXT(Structure):
    _fields_ = [
        ("P1Home", c_ulonglong),
        ("P2Home", c_ulonglong),
        ("P3Home", c_ulonglong),
        ("P4Home", c_ulonglong),
        ("P5Home", c_ulonglong),
        ("P6Home", c_ulonglong),
        ("ContextFlags", c_ulong),
        ("MxCsr", c_ulong),
        ("SegCs", c_ushort),
        ("SegDs", c_ushort),
        ("SegEs", c_ushort),
        ("SegFs", c_ushort),
        ("SegGs", c_ushort),
        ("SegSs", c_ushort),
        ("EFlags", c_ulong),
        ("Dr0", c_ulonglong),
        ("Dr1", c_ulonglong),
        ("Dr2", c_ulonglong),
        ("Dr3", c_ulonglong),
        ("Dr6", c_ulonglong),
        ("Dr7", c_ulonglong),
        ("Rax", c_ulonglong),
        ("Rcx", c_ulonglong),
        ("Rdx", c_ulonglong),
        ("Rbx", c_ulonglong),
        ("Rsp", c_ulonglong),
        ("Rbp", c_ulonglong),
        ("Rsi", c_ulonglong),
        ("Rdi", c_ulonglong),
        ("R8", c_ulonglong),
        ("R9", c_ulonglong),
        ("R10", c_ulonglong),
        ("R11", c_ulonglong),
        ("R12", c_ulonglong),
        ("R13", c_ulonglong),
        ("R14", c_ulonglong),
        ("R15", c_ulonglong),
        ("Rip", c_ulonglong),
        ("FltSave", XMM_SAVE_AREA32),
        ("VectorRegister", M128A * 26),
        ("VectorControl", c_ulonglong),
        ("DebugControl", c_ulonglong),
        ("LastBranchToRip", c_ulonglong),
        ("LastBranchFromRip", c_ulonglong),
        ("LastExceptionToRip", c_ulonglong),
        ("LastExceptionFromRip", c_ulonglong)
    ]

class UnwindCode(Structure):
    _fields_ = [
        ("CodeOffset", c_ubyte),
        ("UnwindOp", c_ubyte, 4),
        ("OpInfo", c_ubyte, 4)
    ]

class UnwindInfo(Structure):
    _fields_ = [
        ("Version", c_ubyte, 3),
        ("Flags", c_ubyte, 5),
        ("SizeOfProlog", c_ubyte),
        ("CountOfCodes", c_ubyte),
        ("FrameRegister", c_ubyte, 4),
        ("FrameOffset", c_ubyte, 4),
        ("UnwindCode", UnwindCode * 1)
    ]

class UnwinderContext(Structure):
    _fields_ = [
        ("rip", c_uint64),
        ("rsp", c_uint64),
        ("rbp", c_uint64),
        ("registers", c_uint64 * 16)
    ]

class SimpleContext(Structure):
    _fields_ = [
        ("Rax", c_ulonglong),
        ("Rcx", c_ulonglong),
        ("Rdx", c_ulonglong),
        ("Rbx", c_ulonglong),
        ("Rsp", c_ulonglong),
        ("Rbp", c_ulonglong),
        ("Rip", c_ulonglong),
    ]

def test_unwinder():
    dll_path = os.path.abspath(os.path.join(os.path.dirname(os.path.dirname(__file__)), 'build', 'unwinder.dll'))
    unwinder = ctypes.CDLL(dll_path)
    
    unwinder.test_and_unwind.argtypes = [] 
    unwinder.test_and_unwind.restype = c_bool
    unwinder.test_leaf.argtypes = []  
    unwinder.test_leaf.restype = c_bool
    
    print("\nTesting full unwind...")
    if unwinder.test_and_unwind():
        print("Full unwind test completed successfully")
    else:
        print("Full unwind test failed")
        
    print("\nTesting leaf function handler...")
    if unwinder.test_leaf():
        print("Leaf function test completed successfully")
    else:
        print("Leaf function test failed")

if __name__ == "__main__":
    test_unwinder()