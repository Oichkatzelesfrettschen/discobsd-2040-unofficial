.syntax unified
.cpu cortex-m0plus
.thumb
.text
.balign 2
.global api_xor
.type api_xor,%function
.thumb_func
api_xor:
    eors r0, r1
    eors r0, r2
    eors r0, r3
    bx lr
.size api_xor, .-api_xor
.global __libc_impl_api_xor
.set __libc_impl_api_xor,api_xor
.type __libc_impl_api_xor,%function
.size __libc_impl_api_xor,.-api_xor
