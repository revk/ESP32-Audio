// Settings lib
void revk_settings_load (const char *tag, const char *appname);
const char *revk_setting_dump (int level);
void revk_settings_commit (void);
#ifndef  CONFIG_REVK_OLD_SETTINGS
revk_settings_t *revk_settings_find (const char *name, int *index);
int revk_settings_set (revk_settings_t *);
char *revk_settings_text (revk_settings_t * s, int index, int *lenp);
#endif
