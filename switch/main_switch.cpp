
/*
 * REminiscence - Flashback interpreter
 * Copyright (C) 2005-2015 Gregory Montoir (cyx@users.sourceforge.net)
 */

#include <SDL.h>
#include <ctype.h>
#include <getopt.h>
#include <sys/stat.h>
#include <switch.h>
#include "file.h"
#include "fs.h"
#include "game.h"
#include "scaler.h"
#include "systemstub.h"
#include "util.h"

static const char *USAGE =
	"REminiscence - Flashback Interpreter\n"
	"Usage: %s [OPTIONS]...\n"
	"  --datapath=PATH   Path to data files (default 'DATA')\n"
	"  --savepath=PATH   Path to save files (default '.')\n"
	"  --levelnum=NUM    Start to level, bypass introduction\n"
	"  --fullscreen      Fullscreen display\n"
	"  --scaler=NAME@X   Graphics scaler (default 'scale@3')\n"
	"  --language=LANG   Language (fr,en,de,sp,it)\n"
;

static int detectVersion(FileSystem *fs) {
	static const struct {
		const char *filename;
		int type;
		const char *name;
	} table[] = {
		{ "DEMO_UK.ABA", kResourceTypeDOS, "DOS (Demo)" },
		{ "INTRO.SEQ", kResourceTypeDOS, "DOS CD" },
		{ "LEVEL1.MAP", kResourceTypeDOS, "DOS" },
		{ "LEVEL1.LEV", kResourceTypeAmiga, "Amiga" },
		{ "DEMO.LEV", kResourceTypeAmiga, "Amiga (Demo)" },
		{ 0, -1, 0 }
	};
	for (int i = 0; table[i].filename; ++i) {
		File f;
		if (f.open(table[i].filename, "rb", fs)) {
			debug(DBG_INFO, "Detected %s version", table[i].name);
			return table[i].type;
		}
	}
	return -1;
}

static Language detectLanguage(FileSystem *fs) {
	static const struct {
		const char *filename;
		Language language;
	} table[] = {
		// PC
		{ "ENGCINE.TXT", LANG_EN },
		{ "FR_CINE.TXT", LANG_FR },
		{ "GERCINE.TXT", LANG_DE },
		{ "SPACINE.TXT", LANG_SP },
		{ "ITACINE.TXT", LANG_IT },
		// Amiga
		{ "FRCINE.TXT", LANG_FR },
		{ 0, LANG_EN }
	};
	for (int i = 0; table[i].filename; ++i) {
		File f;
		if (f.open(table[i].filename, "rb", fs)) {
			return table[i].language;
		}
	}
	return LANG_EN;
}

Options g_options;
const char *g_caption = "REminiscence";

static void initOptions() {
	// defaults
	g_options.bypass_protection = true;
	g_options.play_disabled_cutscenes = false;
	g_options.enable_password_menu = false;
	g_options.fade_out_palette = true;
	g_options.use_text_cutscenes = false;
	g_options.use_seq_cutscenes = true;
	// read configuration file
	struct {
		const char *name;
		bool *value;
	} opts[] = {
		{ "bypass_protection", &g_options.bypass_protection },
		{ "play_disabled_cutscenes", &g_options.play_disabled_cutscenes },
		{ "enable_password_menu", &g_options.enable_password_menu },
		{ "fade_out_palette", &g_options.fade_out_palette },
		{ "use_tiledata", &g_options.use_tiledata },
		{ "use_text_cutscenes", &g_options.use_text_cutscenes },
		{ "use_seq_cutscenes", &g_options.use_seq_cutscenes },
		{ 0, 0 }
	};
	static const char *filename = "rs.cfg";
	FILE *fp = fopen(filename, "rb");
	if (fp) {
		char buf[256];
		while (fgets(buf, sizeof(buf), fp)) {
			if (buf[0] == '#') {
				continue;
			}
			const char *p = strchr(buf, '=');
			if (p) {
				++p;
				while (*p && isspace(*p)) {
					++p;
				}
				if (*p) {
					const bool value = (*p == 't' || *p == 'T' || *p == '1');
					for (int i = 0; opts[i].name; ++i) {
						if (strncmp(buf, opts[i].name, strlen(opts[i].name)) == 0) {
							*opts[i].value = value;
							break;
						}
					}
				}
			}
		}
		fclose(fp);
	}
}

static void parseScaler(char *name, ScalerParameters *scalerParameters) {
	struct {
		const char *name;
		int type;
	} scalers[] = {
		{ "point", kScalerTypePoint },
		{ "linear", kScalerTypeLinear },
		{ "scale", kScalerTypeInternal },
		{ 0, -1 }
	};
	bool found = false;
	char *sep = strchr(name, '@');
	if (sep) {
		*sep = 0;
	}
	for (int i = 0; scalers[i].name; ++i) {
		if (strcmp(scalers[i].name, name) == 0) {
			scalerParameters->type = (ScalerType)scalers[i].type;
			found = true;
			break;
		}
	}
	if (!found) {
		char libname[32];
		snprintf(libname, sizeof(libname), "scaler_%s", name);
		const Scaler *scaler = findScaler(libname);
		if (scaler) {
			scalerParameters->type = kScalerTypeExternal;
			scalerParameters->scaler = scaler;
		} else {
			warning("Scaler '%s' not found, using default", libname);
		}
	}
	if (sep) {
		scalerParameters->factor = atoi(sep + 1);
	}
}

int main(int argc, char *argv[]) {

	consoleDebugInit(debugDevice_SVC);
	stdout = stderr;

	const char *dataPath = "DATA";
	const char *savePath = ".";
	int levelNum = 0;

	bool fullscreen = true;
	ScalerParameters scalerParameters = ScalerParameters::defaults();
	scalerParameters.factor = 1;
	scalerParameters.type = kScalerTypeExternal;
	scalerParameters.scaler = NULL;

	int forcedLanguage = -1;
	int demoNum = -1;
	if (argc == 2) {
		// data path as the only command line argument
		struct stat st;
		if (stat(argv[1], &st) == 0 && S_ISDIR(st.st_mode)) {
			dataPath = strdup(argv[1]);
		}
	}
	while (1) {
		static struct option options[] = {
			{ "datapath",   required_argument, 0, 1 },
			{ "savepath",   required_argument, 0, 2 },
			{ "levelnum",   required_argument, 0, 3 },
			{ "fullscreen", no_argument,       0, 4 },
			{ "scaler",     required_argument, 0, 5 },
			{ "language",   required_argument, 0, 6 },
			{ "playdemo",   required_argument, 0, 7 },
			{ 0, 0, 0, 0 }
		};
		int index;
		const int c = getopt_long(argc, argv, "", options, &index);
		if (c == -1) {
			break;
		}
		switch (c) {
		case 1:
			dataPath = strdup(optarg);
			break;
		case 2:
			savePath = strdup(optarg);
			break;
		case 3:
			levelNum = atoi(optarg);
			break;
		case 4:
			fullscreen = true;
			break;
		case 5:
			parseScaler(optarg, &scalerParameters);
			break;
		case 6: {
				static const struct {
					int lang;
					const char *str;
				} languages[] = {
					{ LANG_FR, "FR" },
					{ LANG_EN, "EN" },
					{ LANG_DE, "DE" },
					{ LANG_SP, "SP" },
					{ LANG_IT, "IT" },
					{ -1, 0 }
				};
				for (int i = 0; languages[i].str; ++i) {
					if (strcasecmp(languages[i].str, optarg) == 0) {
						forcedLanguage = languages[i].lang;
						break;
					}
				}
			}
			break;
		case 7:
			demoNum = atoi(optarg);
			break;
		default:
			printf(USAGE, argv[0]);
			return 0;
		}
	}
	initOptions();
	g_debugMask = DBG_INFO; // DBG_CUT | DBG_VIDEO | DBG_RES | DBG_MENU | DBG_PGE | DBG_GAME | DBG_UNPACK | DBG_COL | DBG_MOD | DBG_SFX | DBG_FILE;
	FileSystem fs(dataPath);
	const int version = detectVersion(&fs);
	if (version == -1) {
		error("Unable to find data files, check that all required files are present");
		return -1;
	}
	const Language language = (forcedLanguage == -1) ? detectLanguage(&fs) : (Language)forcedLanguage;
	SystemStub *stub = SystemStub_SDL_create();
	Game *g = new Game(stub, &fs, savePath, levelNum, demoNum, (ResourceType)version, language);
	stub->init(g_caption, Video::GAMESCREEN_W, Video::GAMESCREEN_H, fullscreen, &scalerParameters);
	g->run();
	delete g;
	stub->destroy();
	delete stub;
	return 0;
}
