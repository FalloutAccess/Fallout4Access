# Xbyak Usage Guide for F4SE Plugins

Guide for using [xbyak](https://github.com/herumi/xbyak) to create custom trampolines and mid-function hooks in F4SE plugins.

## What CommonLibF4 Already Provides

CommonLibF4 includes built-in hook support via `REL::Hook` and `REL::Trampoline`:

| Feature | CommonLibF4 Support |
|---------|---------------------|
| CALL5 hook (5-byte `E8` call) | ✅ `REL::Hook` auto-detects |
| CALL6 hook (6-byte `FF 15` call) | ✅ `REL::Hook` auto-detects |
| JMP5 hook (5-byte `E9` jump) | ✅ `REL::Hook` auto-detects |
| JMP6 hook (6-byte `FF 25` jump) | ✅ `REL::Hook` auto-detects |
| VFT hook (vtable replacement) | ✅ `REL::HookVFT` |
| Mid-function hook | ❌ Need xbyak |
| Register access/modification | ❌ Need xbyak |
| Custom calling conventions | ❌ Need xbyak |

## When You Need Xbyak

Use xbyak when you need to:

1. **Mid-function hooks** - Intercept execution at arbitrary points, not just at CALL/JMP instructions
2. **Access register values** - Read/modify CPU registers at hook point (e.g., get `rcx` parameter before it's overwritten)
3. **Conditional hooks** - Execute different code paths based on runtime values
4. **Custom prolog/epilog** - Save/restore additional registers or stack state
5. **Multi-instruction patches** - Replace more than 5-6 bytes of code

---

## Xbyak Basics

### Installation (xmake)

```lua
add_requires("xbyak")
target("fallout4access")
    add_packages("xbyak")
```

### Basic Structure

```cpp
#include <xbyak/xbyak.h>

struct MyHook : Xbyak::CodeGenerator {
    MyHook() : Xbyak::CodeGenerator(4096) {  // 4KB code buffer
        // Assembly instructions go here
        mov(rax, rcx);
        ret();
    }
};

// Usage:
MyHook code;
auto func = code.getCode<void(*)()>();
```

### Passing Buffer from Trampoline

```cpp
struct MyHook : Xbyak::CodeGenerator {
    MyHook(void* buf, std::size_t size)
        : Xbyak::CodeGenerator(size, buf)
    {
        // Code uses pre-allocated buffer
    }
};

// Allocate from CommonLibF4 trampoline:
auto& trampoline = REL::GetTrampoline();
void* buf = trampoline.allocate(128);
MyHook hook(buf, 128);
```

---

## x64 Windows Calling Convention

### Parameter Registers

| Parameter | Integer | Float |
|-----------|---------|-------|
| 1st | `rcx` | `xmm0` |
| 2nd | `rdx` | `xmm1` |
| 3rd | `r8` | `xmm2` |
| 4th | `r9` | `xmm3` |
| 5th+ | Stack | Stack |

### Volatile (Caller-Save) Registers

Must save before calling functions if you need them preserved:
- `rax`, `rcx`, `rdx`, `r8`, `r9`, `r10`, `r11`
- `xmm0`-`xmm5`

### Non-Volatile (Callee-Save) Registers

Automatically preserved by called functions:
- `rbx`, `rbp`, `rdi`, `rsi`, `r12`, `r13`, `r14`, `r15`
- `xmm6`-`xmm15`

### Shadow Space

Always allocate 32 bytes (0x20) of stack space before CALL:

```cpp
sub(rsp, 0x20);  // Allocate shadow space
call(ptr[rip + funcLabel]);
add(rsp, 0x20);  // Deallocate
```

---

## Common Patterns

### Pattern 1: Simple Mid-Function Hook

Intercept at a specific address, call a C++ function, continue execution:

```cpp
// C++ handler function
void MyHandler(RE::Actor* actor, int value) {
    // Process intercepted values
    logger::info("Actor: {}, Value: {}", actor->GetName(), value);
}

struct MidFunctionHook : Xbyak::CodeGenerator {
    MidFunctionHook(std::uintptr_t returnAddr) {
        Xbyak::Label funcLabel, retnLabel;

        // Save volatile registers we'll clobber
        push(rcx);
        push(rdx);
        sub(rsp, 0x20);  // Shadow space

        // rcx and rdx already have the values we want
        // (they were set by the original code before our hook)
        call(ptr[rip + funcLabel]);

        add(rsp, 0x20);
        pop(rdx);
        pop(rcx);

        // Execute the instructions we overwrote (if any)
        // ... stolen bytes here ...

        // Jump back to original code
        jmp(ptr[rip + retnLabel]);

        // Data section
        L(funcLabel);
        dq(reinterpret_cast<std::uintptr_t>(&MyHandler));

        L(retnLabel);
        dq(returnAddr);
    }
};
```

### Pattern 2: Capture Register Value

Read a register value without modifying program flow:

```cpp
// Global to receive the captured value
std::atomic<RE::TESObjectREFR*> g_lastTarget{ nullptr };

void CaptureTarget(RE::TESObjectREFR* target) {
    g_lastTarget.store(target);
}

struct CaptureRegisterHook : Xbyak::CodeGenerator {
    CaptureRegisterHook(std::uintptr_t returnAddr) {
        Xbyak::Label funcLabel, retnLabel;

        // Save all volatile registers (we don't know what original code needs)
        push(rax);
        push(rcx);
        push(rdx);
        push(r8);
        push(r9);
        push(r10);
        push(r11);
        sub(rsp, 0x20);

        // rcx contains the value we want to capture
        // (it's already in rcx, the first parameter slot)
        call(ptr[rip + funcLabel]);

        add(rsp, 0x20);
        pop(r11);
        pop(r10);
        pop(r9);
        pop(r8);
        pop(rdx);
        pop(rcx);
        pop(rax);

        // Jump back
        jmp(ptr[rip + retnLabel]);

        L(funcLabel);
        dq(reinterpret_cast<std::uintptr_t>(&CaptureTarget));

        L(retnLabel);
        dq(returnAddr);
    }
};
```

### Pattern 3: Conditional Hook (Modify Flow)

Check a condition and optionally skip original code:

```cpp
// Returns true to skip original, false to continue
bool ShouldSkip(RE::Actor* actor) {
    return actor->IsPlayer();
}

struct ConditionalHook : Xbyak::CodeGenerator {
    ConditionalHook(std::uintptr_t skipAddr, std::uintptr_t continueAddr) {
        Xbyak::Label funcLabel, skipLabel, continueLabel;

        push(rcx);
        push(rdx);
        sub(rsp, 0x20);

        // rcx = actor pointer
        call(ptr[rip + funcLabel]);

        add(rsp, 0x20);
        pop(rdx);
        pop(rcx);

        // Check return value (in al/rax)
        test(al, al);
        jnz(skipLabel);  // If true, skip

        // Continue to original code
        jmp(ptr[rip + continueLabel]);

        L(skipLabel);
        jmp(ptr[rip + skipLabel]);

        L(funcLabel);
        dq(reinterpret_cast<std::uintptr_t>(&ShouldSkip));

        L(continueLabel);
        dq(continueAddr);

        // Reuse label for skip target
        L(skipLabel);
        dq(skipAddr);
    }
};
```

### Pattern 4: Replace Stolen Bytes

When your hook overwrites multi-byte instructions:

```cpp
struct StolenBytesHook : Xbyak::CodeGenerator {
    StolenBytesHook(std::uintptr_t returnAddr) {
        Xbyak::Label funcLabel, retnLabel;

        // Call our handler
        push(rcx);
        sub(rsp, 0x20);
        call(ptr[rip + funcLabel]);
        add(rsp, 0x20);
        pop(rcx);

        // Execute the instructions we overwrote:
        // Original bytes: 48 89 5C 24 08  = mov [rsp+8], rbx
        //                 48 89 6C 24 10  = mov [rsp+10], rbp
        mov(ptr[rsp + 0x08], rbx);
        mov(ptr[rsp + 0x10], rbp);

        // Return to instruction after stolen bytes
        jmp(ptr[rip + retnLabel]);

        L(funcLabel);
        dq(reinterpret_cast<std::uintptr_t>(&MyHandler));

        L(retnLabel);
        dq(returnAddr);
    }
};
```

---

## Installing the Hook

### Method 1: Using CommonLibF4 Trampoline

```cpp
void InstallHooks() {
    auto& trampoline = REL::GetTrampoline();

    // Find target address (using Address Library ID or offset)
    REL::ID targetID{ 12345 };  // Replace with actual ID
    std::uintptr_t targetAddr = targetID.address() + 0x42;  // + offset to hook point
    std::uintptr_t returnAddr = targetAddr + 5;  // After our 5-byte jump

    // Allocate and generate hook code
    void* codeBuf = trampoline.allocate(128);
    MidFunctionHook hook(codeBuf, 128, returnAddr);

    // Write jump to our hook
    trampoline.write_jmp<5>(targetAddr, reinterpret_cast<std::uintptr_t>(codeBuf));

    logger::info("Hook installed at {:X}", targetAddr);
}
```

### Method 2: Direct Memory Write

```cpp
void InstallHookDirect() {
    std::uintptr_t targetAddr = /* ... */;
    std::uintptr_t returnAddr = targetAddr + 5;

    // Generate code (uses internal buffer)
    static MidFunctionHook hook(returnAddr);
    auto hookAddr = reinterpret_cast<std::uintptr_t>(hook.getCode());

    // Write 5-byte relative jump
    REL::ASM::JMP5 jmp(targetAddr, hookAddr);
    REL::WriteSafe(targetAddr, std::span{ reinterpret_cast<std::byte*>(&jmp), sizeof(jmp) });
}
```

---

## Xbyak Instruction Reference

### Data Movement

```cpp
mov(rax, rcx);           // Register to register
mov(rax, 0x12345678);    // Immediate to register
mov(rax, ptr[rcx]);      // Memory to register
mov(ptr[rcx], rax);      // Register to memory
mov(rax, ptr[rcx + 8]);  // Memory + offset
mov(rax, ptr[rip + label]); // RIP-relative
lea(rax, ptr[rcx + rdx*8]); // Load effective address
```

### Stack Operations

```cpp
push(rax);
pop(rax);
sub(rsp, 0x20);  // Allocate stack
add(rsp, 0x20);  // Deallocate stack
```

### Control Flow

```cpp
call(ptr[rip + label]);  // Call via label
jmp(ptr[rip + label]);   // Jump via label
ret();                   // Return

// Conditional jumps
test(al, al);
jz(label);   // Jump if zero
jnz(label);  // Jump if not zero
je(label);   // Jump if equal
jne(label);  // Jump if not equal
```

### Arithmetic

```cpp
add(rax, rbx);
sub(rax, 0x10);
inc(rax);
dec(rax);
xor_(rax, rax);  // Note: xor_ not xor (reserved keyword)
and_(rax, 0xFF); // Note: and_ not and
or_(rax, rbx);   // Note: or_ not or
```

### Labels and Data

```cpp
Xbyak::Label myLabel;

L(myLabel);      // Define label position
jmp(myLabel);    // Jump to label

// Data definitions
db(0x90);        // Define byte (NOP)
dw(0x1234);      // Define word (16-bit)
dd(0x12345678);  // Define dword (32-bit)
dq(address);     // Define qword (64-bit)
```

---

## Accessibility Plugin Use Cases

### Use Case 1: Intercept UI Text Updates

Hook when game updates text fields to capture the new text for speech:

```cpp
void OnTextUpdate(RE::GFxValue* textField, const char* newText) {
    if (newText && *newText) {
        SpeechManager::GetSingleton()->Speak(newText);
    }
}

struct TextUpdateHook : Xbyak::CodeGenerator {
    TextUpdateHook(std::uintptr_t returnAddr) {
        // rcx = GFxValue* (text field)
        // rdx = const char* (new text)

        push(rcx);
        push(rdx);
        sub(rsp, 0x20);

        call(ptr[rip + funcLabel]);

        add(rsp, 0x20);
        pop(rdx);
        pop(rcx);

        jmp(ptr[rip + retnLabel]);

        L(funcLabel);
        dq(reinterpret_cast<std::uintptr_t>(&OnTextUpdate));

        L(retnLabel);
        dq(returnAddr);
    }
};
```

### Use Case 2: Intercept Menu Navigation Events

Capture when player navigates menus before the visual update:

```cpp
void OnMenuNavigate(RE::IMenu* menu, int newIndex, int oldIndex) {
    auto name = menu->GetRuntimeData().name;
    logger::info("Menu {} navigated: {} -> {}", name, oldIndex, newIndex);
}

struct MenuNavigateHook : Xbyak::CodeGenerator {
    MenuNavigateHook(std::uintptr_t returnAddr) {
        // Assuming: rcx=menu, edx=newIndex, r8d=oldIndex

        push(rcx);
        push(rdx);
        push(r8);
        sub(rsp, 0x20);

        call(ptr[rip + funcLabel]);

        add(rsp, 0x20);
        pop(r8);
        pop(rdx);
        pop(rcx);

        jmp(ptr[rip + retnLabel]);

        L(funcLabel);
        dq(reinterpret_cast<std::uintptr_t>(&OnMenuNavigate));

        L(retnLabel);
        dq(returnAddr);
    }
};
```

### Use Case 3: Intercept Object Activation

Capture when player activates an object to announce it:

```cpp
void OnActivate(RE::TESObjectREFR* activator, RE::TESObjectREFR* target) {
    if (activator->IsPlayer() && target) {
        auto name = target->GetDisplayFullName();
        SpeechManager::GetSingleton()->Speak(name);
    }
}
```

---

## References

- [xbyak GitHub](https://github.com/herumi/xbyak)
- [PerkApplyBugFixSSE](https://github.com/aers/PerkApplyBugFixSSE) - SKSE mid-function hook example
- [expired6978/F4SEPlugins](https://github.com/expired6978/F4SEPlugins) - F4SE hook examples
- [x64 Calling Convention (Microsoft)](https://docs.microsoft.com/en-us/cpp/build/x64-calling-convention)
- [Kyle Halladay - X64 Function Hooking](https://kylehalladay.com/blog/2020/11/13/Hooking-By-Example.html)
