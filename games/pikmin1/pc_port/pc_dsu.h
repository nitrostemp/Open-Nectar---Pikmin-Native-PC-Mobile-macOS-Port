#ifndef PC_DSU_H
#define PC_DSU_H

/* DSU (Cemuhook) client. Reads one controller slot from a DSU server on
   127.0.0.1 (NSO GC Driver, DS4Windows, BetterJoy...) and exposes it as an SDL
   virtual game controller, so the normal pad path, bindings and co-op slots
   pick it up like any other pad. Needs no OS-level virtual HID device.

   Buttons are read by GameCube label as NSO GC Driver sends them: A, B, X, Y,
   Z, L, R, Start, D-pad, plus ZL, Home and Capture for binding. */

/// Every input poll, before SDL_PollEvent. Follows the DSU settings: opens or
/// closes the socket and attaches or detaches the virtual pad as needed.
void pc_dsu_update(void);
/// The server reports a controller in the configured slot.
bool pc_dsu_connected(void);
/// Detach the virtual pad and close the socket. Before SDL_Quit.
void pc_dsu_shutdown(void);

#endif // PC_DSU_H
