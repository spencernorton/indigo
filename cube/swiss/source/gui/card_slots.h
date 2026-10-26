#ifndef CARD_SLOTS_H
#define CARD_SLOTS_H

/* The memory card slots around a GameID switch, for Indigo's game details.
 *
 * Sends a game's ID to the memory card emulators in the slots (the MemCard
 * PRO GC and other MMCE cards) as gameid.c's gameID_early_set does, then
 * follows each card through its switch (ui_card_slots.c) so the details read
 * a card only once it holds the game's card, never in the middle, and write
 * one only then. Every wait is bounded. Plain memory cards are read as they
 * are, as before. */

#include <stdbool.h>
#include "gcm.h"

/* A game's details open: its ID to the cards (an ID waits behind a switch
 * under way, and goes out after it). */
void CardSlots_RequestGame(const DiskHeader *header);

/* Each idle retrace while details are open, or while a launch waits: follows
 * the switches, reads a card when one is due, sends an ID that waited. True
 * when a slot finished switching (ready, or given up): read the saves again. */
bool CardSlots_Poll(void);

/* Game gameId's details may read the card in slot (0 A, 1 B) now. */
bool CardSlots_ReadableFor(int slot, const char gameId[6]);

/* A card is switching to game gameId, or its ID waits to go out: its saves
 * come later. */
bool CardSlots_WaitingFor(const char gameId[6]);

/* The card in slot was given up on while switching to game gameId (its
 * saves are missing from a scan, which is then incomplete). */
bool CardSlots_FailedFor(int slot, const char gameId[6]);

/* A save for game gameId may be written to the card in slot now. */
bool CardSlots_WritableFor(int slot, const char gameId[6]);

/* An ID still waits to go out (a launch waits for it, polling). */
bool CardSlots_IdWaiting(void);

/* What the slots did since Indigo started, a line an event (the first 8 KB):
 * the times, the IDs, the presence changes, the status bytes and the reads. */
const char *CardSlots_Trace(void);

/* Diagnostic branch: explain whether a completed scan hides the Saves row. */
void CardSlots_TraceScan(unsigned slotsRead, unsigned copies, bool partial,
	bool waiting, bool failed);

#endif
