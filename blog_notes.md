## why are stack pointer `sp` 16-byte aligned?
because a stack frame should contain at least `ra` and previous `fp`, each are 8 bytes or 64 bits.
That means a stack frame is at least 16 bytes long.

`sp` points to the lowest byte of the current stack frame. (stack grows from high address to low address)
`fp` points to the lowest byte of the previous stack frame. (equal to previous sp)

### start of function body
to grow stack by 16 bytes, use `addi sp, sp, -16`. notice it's -16 instead of 16.
fp is calculated by `addi s0, sp, 16`, where s0 is fp and 16 is the size of current stack frame.

### end of function body
to shrink stack by 16, use `addi sp, sp, 16`
