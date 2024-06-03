.global song_of_storms
.global song_of_storms_end
.global a_loop
.global a_loop_end
.global b_loop
.global b_loop_end
.global d_loop
.global d_loop_end
.global d2_loop
.global d2_loop_end
.global f_loop
.global f_loop_end

.text
song_of_storms:
.incbin "../data/Song_of_Storms.bin"
song_of_storms_end:

a_loop:
.incbin "../data/Ocarina_A_loop.bin"
a_loop_end:

b_loop:
.incbin "../data/Ocarina_B_loop.bin"
b_loop_end:

d_loop:
.incbin "../data/Ocarina_D_loop.bin"
d_loop_end:

d2_loop:
.incbin "../data/Ocarina_D2_loop.bin"
d2_loop_end:

f_loop:
.incbin "../data/Ocarina_F_loop.bin"
f_loop_end: