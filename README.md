# SAM Coupé Scroller

This project attempts to implement a full-screen 50Hz scrolling title for the SAM Coupé. It is therefore possibly iterating towards a two-thirds screen 25Hz scrolling title for the SAM Coupé.

## Technical Background

The SAM Coupé is an 8-bit computer from the 1980s with a 6Mhz Z80 and a 24kb native-mode frame buffer which does not support any form of hardware scrolling, accelerated drawing or other adornments — though the buffer can be set to start at any 32kb boundary. Video RAM sits in the CPU's address space and is subject to arbitrated access; the CPU may access RAM only once in every four cycles during the border and once in every eight cycles during the active display area.

The display is active on 192 lines out of 312, for 256 out of 384 cycles per line.

That gives 120 lines where the CPU may access RAM 96 times a line plus 192 lines where the CPU may access RAM: (i) 32 times during the border; plus (ii) 32 times during the active display; for 64 accesses total. So there are a total of 120×96 + 192×64 = 23,808 available access windows per frame.

Hence it would be impossible to touch every one of the 24,576 bytes of the display in a single frame even if the CPU didn't also have its own instruction stream to fetch.

An approximate rule of thumb is that performant code in ideal conditions (no overdraw, no read-modify-write, etc) can update a quarter to a third of the display per frame.

## Achieving 'performant code'

The Z80 offers only one way to write to memory with automatic address advancement, which is via the stack pointer. So this project follows an endless stream of predecessors in using the stack pointer for graphics output.

It also takes advantage of ahead-of-time compilation for all graphical assets in order to embed pixel data in the instruction stream. Some attempt is made at better-than-dumb register allocation to reduce repetition of pixel data.

### No Stack

During tile map output the stack pointer is used _exclusively_ for rectangular graphics output, generally with DE as an ersatz link register for subroutine calls. Interrupts are disabled for the program's entire runtime.

For frame synchronisation the current state of the SAM's line interrupt is polled until a recurring target of 191 (i.e. the end of the final visible line) is hit.

## Minimising Draw Area

This implementation — like many before — uses a tile map to reduce the area redraw and scrolls on one axis only; if moving the camera to the right in a tile-sized step then the tile at (x, y) needs to be redrawn only if the tile at (x+1, y) is different.

A precomputed table of difference flags records the result of those comparisons out ahead of time.

Different back buffers are used for the current map offet in sub-tile increments. This permits scrolling in 2-pixel increments rather than in whole-tile increments.

## Adding Sprites

Sprites are precompiled but don't use the stack pointer for output since it makes masking difficult. They mark a tile-map aligned grid of dirty flags so that they're erased automatically during the standard tile output routine.

Those dirty flags are ORd with the precomputed differences set when scrolling, but used in isolation when the screen is static.

### Net Corollary: 50Hz or bust

The SAM, like the Spectrum before it, signals its interrupts for a fixed amount of time then silently stops signalling. Since this program polls for the appropriate interrupt it can therefore only ever synchronise to the _next_ interrupt with no idea of how many have fallen unobserved. The working hope is that game maps and sprite totals can be massaged as necessary to resolve areas of slowdown.

The use of different display buffers for different sub-tile scroll offsets means that there is no back buffer if the screen has not scrolled. Those frames must race the raster.
