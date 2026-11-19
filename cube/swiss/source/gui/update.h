#ifndef UPDATE_H
#define UPDATE_H

/* Updating Indigo from Settings > Network (gui/update.c): one checked
 * download, verify and install run behind two rows, fed either from the
 * update server over plain HTTP or from the SMB share. The reading and
 * the deciding are in gui/ui_update.h; the design is
 * docs/dev/software-update-design.md. */

/* The update server when Update Server is empty: nothing, until a public
 * plain-HTTP host serves the release files (the console has no DNS and no
 * TLS, so the address will be an IPv4 address). Until then Check for
 * updates asks for one, and a mirror on your own network works as it is. */
#define UPDATE_DEFAULT_SERVER ""

enum update_source {
	UPDATE_SOURCE_HTTP,
	UPDATE_SOURCE_SMB
};

/* One whole check, download, verify and install, in the press that asked:
 * it shows its own progress, asks before it downloads and again before it
 * installs, and leaves the card as it found it unless it finished. */
void updateRun(enum update_source source);

#endif
