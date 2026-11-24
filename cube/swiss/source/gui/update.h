#ifndef UPDATE_H
#define UPDATE_H

/* GitHub Pages publishes these IPv4 addresses. The publisher signature
 * authenticates every response, including files from a user-supplied mirror. */
#define UPDATE_DEFAULT_SERVER "185.199.108.153/updates/"
#define UPDATE_DEFAULT_HOST "indigo-updates.norvitech.com"

enum update_source { UPDATE_SOURCE_HTTP, UPDATE_SOURCE_SMB };
void updateRun(enum update_source source);
void updateStartupBoot(int argc, char **argv);
void updateStartupPoll(void);

#endif
