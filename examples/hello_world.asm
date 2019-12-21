start:
  jz 0, loopcond
loop:
  out *message @ i
  add *i, 1, *i
loopcond:
  eq *i, end, *temp
  jz 0 @ temp, loop
loopend:
  halt
message:
  .ascii "Hello, World!\n"
end:
