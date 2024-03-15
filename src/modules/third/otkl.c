    /* Copyright (C) All Rights Reserved
    ** Written by Gottem <support@gottem.nl>
    ** Website: https://gottem.nl/unreal
    ** License: https://gottem.nl/unreal/license
    */

    /*** <<<MODULE MANAGER START>>>
    module {
    	documentation "https://gottem.nl/unreal/man/otkl";
    	troubleshooting "In case of problems, check the FAQ at https://gottem.nl/unreal/halp or e-mail me at support@gottem.nl";
    	min-unrealircd-version "6.*";
    	//max-unrealircd-version "6.*";
    	post-install-text {
    		"The module is installed, now all you need to do is add a 'loadmodule' line to your config file:";
    		"loadmodule \"third/otkl\";";
    		"Then /rehash the IRCd.";
    		"For usage information, refer to the module's documentation found at: https://gottem.nl/unreal/man/otkl";
    	}
    }
    *** <<<MODULE MANAGER END>>>
    */

    // One include for all cross-platform compatibility thangs
    #include "unrealircd.h"

    // Command strings
    #define MSG_OGLINE "OGLINE" // Regular G-Lines
    #define MSG_OZLINE "OZLINE" // Actually a GZ-Line ;]

    #define DEF_NORAISIN "no reason" // Default message y0

    // Dem macros yo
    CMD_FUNC(cmd_ogline); // Register command function
    CMD_FUNC(cmd_ozline); // Register command function

    #define CheckAPIError(apistr, apiobj) \
    	do { \
    		if(!(apiobj)) { \
    			config_error("A critical error occurred on %s for %s: %s", (apistr), MOD_HEADER.name, ModuleGetErrorStr(modinfo->handle)); \
    			return MOD_FAILED; \
    		} \
    	} while(0)

    // Big hecks go here
    typedef struct t_otkline OTKLine;

    struct t_otkline {
    	char flag; // G or Z really
    	char *user; // Ident
    	char *host; // Hostname or IP
    	char *raisin; // Raisin lel
    	char *setby; // Set by who
    	time_t setat; // UNIX time of when it's set
    	OTKLine *next; // Quality linked list
    };

    // Quality fowod declarations
    static void dumpit(Client *client, char **p);
    void otkl_identmd_free(ModData *md);
    void otkl_moddata_free(ModData *md);
    OTKLine *get_otklines(void);
    OTKLine *find_otkline(char *user, char *host, char flag);
    void add_otkline(OTKLine *newotkl);
    void del_otkline(OTKLine *muhotkl);
    void free_all_otklines(OTKLine *OTKList);
    int match_otkline(Client *client, OTKLine *otkline, char flag);
    void free_otkline(OTKLine *otkline);
    void otkl_main(Client *client, int parc, const char *parv[], char flag);
    int otkl_hook_serverconnect(Client *client);
    int otkl_hook_pre_localconnect(Client *client);
    int ban_too_broad(char *usermask, char *hostmask);

    // Muh globals
    ModDataInfo *otklMDI; // To store the OTKLines with &me lol (hack so we don't have to use a .db file or some shit)
    ModDataInfo *otklIdentMDI; // Moddata for the original ident =]
    int OTKCount; // Count em

    // Help string in case someone does just /OGLINE
    static char *muhHalp[] = {
    	/* Special characters:
    	** \002 = bold -- \x02
    	** \037 = underlined -- \x1F
    	*/
    	"*** \002Help on /OGLINE and /OZLINE\002 ***",
    	"Allows privileged opers to set one-time G/GZ-Lines.",
    	"Local Z-Lines are not supported because they seem fairly useless.",
    	"The rules for hostmasks etc are the same as for regular G/GZ-Lines.",
    	"These \"OTKLines\" persist through a rehash, plus servers will",
    	"sync everything they know at link-time too. If any online users",
    	"match a newly added OTKLine, they will be killed first. Then once",
    	"they reconnect, they'll get killed once more before the OTKLine is",
    	"removed from all servers.",
    	" ",
    	"Syntax:",
    	"    \002/OGLINE\002 or \002/OZLINE\002",
    	"        List the currently known OG/OZ-Lines (two different lists)",
    	"    \002/OGLINE\002 [-]\037identmask@hostmask\037 {\037reason\037}",
    	"        Set/remove an OG-Line (reason is required for adding)",
    	"    \002/OZLINE\002 [-]\037identmask@ipmask\037 {\037reason\037}",
    	"        Set/remove an OZ-Line (reason is required for adding)",
    	"    \002/OGLINE\002 \037help\037",
    	"    \002/OZLINE\002 \037help\037",
    	"       View this built-in help",
    	" ",
    	"Examples:",
    	"    \002/OGLINE *@some.domain.* not gonna happen\002",
    	"        Sets a host-based ban",
    	"    \002/OZLINE *@123.123.* nope\002",
    	"        Sets an IP-based ban",
    	"    \002/OZLINE -*@123.123.* nope\002",
    	"        Removes an IP-based ban",
    	NULL
    };

    // Dat dere module header
    ModuleHeader MOD_HEADER = {
    	"third/otkl", // Module name
    	"2.1.0", // Version
    	"Implements one-time TKLines", // Description
    	"Gottem", // Author
    	"unrealircd-6", // Modversion
    };

    // Initialisation routine (register hooks, commands and modes or create structs etc)
    MOD_INIT() {
    	OTKLine *OTKList, *cur; // To initialise the OTKL counter imo tbh fam

    	CheckAPIError("CommandAdd(OGLINE)", CommandAdd(modinfo->handle, MSG_OGLINE, cmd_ogline, MAXPARA, CMD_USER | CMD_SERVER));
    	CheckAPIError("CommandAdd(OZLINE)", CommandAdd(modinfo->handle, MSG_OZLINE, cmd_ozline, MAXPARA, CMD_USER | CMD_SERVER));

    	// Request moddata for storing the original nick
    	ModDataInfo mreq2;
    	memset(&mreq2, 0, sizeof(mreq2));
    	mreq2.type = MODDATATYPE_LOCAL_CLIENT;
    	mreq2.name = "otkl_lastidents"; // Name it
    	mreq2.free = otkl_identmd_free; // Function to free 'em
    	otklIdentMDI = ModDataAdd(modinfo->handle, mreq2);
    	CheckAPIError("ModDataAdd(otkl_lastidents)", otklIdentMDI);

    	OTKCount = 0;
    	if(!(otklMDI = findmoddata_byname("otkl", MODDATATYPE_LOCAL_VARIABLE))) { // Attempt to find active moddata (like in case of a rehash)
    		ModDataInfo mreq; // No moddata, let's request that shit
    		memset(&mreq, 0, sizeof(mreq)); // Set 'em lol
    		mreq.type = MODDATATYPE_LOCAL_VARIABLE;
    		mreq.name = "otkl_list"; // Name it
    		mreq.free = otkl_moddata_free; // Function to free 'em
    		mreq.serialize = NULL;
    		mreq.unserialize = NULL;
    		mreq.sync = 0;
    		otklMDI = ModDataAdd(modinfo->handle, mreq); // Add 'em yo
    		CheckAPIError("ModDataAdd(otkl_list)", otklMDI);
    	}
    	else { // We did get moddata
    		if((OTKList = get_otklines())) { // So load 'em
    			for(cur = OTKList; cur; cur = cur->next) // and iter8 m8
    				OTKCount++; // Ayyy premium countur
    		}
    	}

    	MARK_AS_GLOBAL_MODULE(modinfo);

    	// Add 'em hewks
    	HookAdd(modinfo->handle, HOOKTYPE_SERVER_CONNECT, 0, otkl_hook_serverconnect);
    	HookAdd(modinfo->handle, HOOKTYPE_PRE_LOCAL_CONNECT, 0, otkl_hook_pre_localconnect);
    	return MOD_SUCCESS;
    }

    // Actually load the module here (also command overrides as they may not exist in MOD_INIT yet)
    MOD_LOAD() {
    	return MOD_SUCCESS; // We good
    }

    // Called on unload/rehash obv
    MOD_UNLOAD() {
    	// Not clearing the moddata structs here so we can re-use them easily ;];]
    	return MOD_SUCCESS; // We good
    }

    // Dump a NULL-terminated array of strings to the user (taken from DarkFire IRCd)
    static void dumpit(Client *client, char **p) {
    	if(IsServer(client)) // Bail out early and silently if it's a server =]
    		return;

    	// Using sendto_one() instead of sendnumericfmt() because the latter strips indentation and stuff ;]
    	for(; *p != NULL; p++)
    		sendto_one(client, NULL, ":%s %03d %s :%s", me.name, RPL_TEXT, client->name, *p);
    }

    void otkl_identmd_free(ModData *md) {
    	safe_free(md->ptr);
    }

    // Probably never called but it's a required function
    // The free shit here normally only happens when the client attached to the moddata quits (afaik), but that's us =]
    void otkl_moddata_free(ModData *md) {
    	if(md->ptr) { // r u insaiyan?
    		free_all_otklines(md->ptr); // Free that shit
    		md->ptr = NULL; // Just in case lol
    	}
    }

    CMD_FUNC(cmd_ogline) {
    	otkl_main(client, parc, parv, 'G');
    }

    CMD_FUNC(cmd_ozline) {
    	otkl_main(client, parc, parv, 'Z');
    }

    OTKLine *get_otklines(void) {
    	OTKLine *OTKList = moddata_local_variable(otklMDI).ptr; // Get mod data
    	if(OTKList && OTKList->user) // Sanity check lol
    		return OTKList;
    	return NULL;
    }

    OTKLine *find_otkline(char *user, char *host, char flag) {
    	OTKLine *OTKList, *otkline; // Head and iter8or fam
    	if((OTKList = get_otklines())) { // Check if the list even has entries kek
    		for(otkline = OTKList; otkline; otkline = otkline->next) { // Iter8 em
    			// Let's do strcasecmp() just in case =]
    			if(otkline->flag == flag && !strcasecmp(otkline->user, user) && !strcasecmp(otkline->host, host))
    				return otkline;
    		}
    	}
    	return NULL; // Not found m8
    }

    void add_otkline(OTKLine *newotkl) {
    	OTKLine *OTKList, *otkline; // Head + iter8or imo tbh
    	newotkl->next = NULL; // Cuz inb4rip linked list
    	OTKCount++; // Always increment count
    	if(!(OTKList = get_otklines())) { // If OTKList is NULL...
    		OTKList = newotkl; // ...simply have it point to the newly alloc8ed entry
    		moddata_local_variable(otklMDI).ptr = OTKList; // And st0re em
    		return;
    	}
    	for(otkline = OTKList; otkline && otkline->next; otkline = otkline->next) { } // Get teh last entray
    	otkline->next = newotkl; // Append lol
    }

    void del_otkline(OTKLine *muhotkl) {
    	OTKLine *OTKList, *last, **otkline;
    	if(!(OTKList = get_otklines())) // Ayyy no OTKLines known
    		return;

    	otkline = &OTKList; // Hecks so the ->next chain stays intact
    	if(*otkline == muhotkl) { // If it's the first entry, need to take special precautions ;]
    		last = *otkline; // Get the entry pointur
    		*otkline = last->next; // Set the iterat0r to the next one
    		free_otkline(last); // Free that shit
    		moddata_local_variable(otklMDI).ptr = *otkline; // Cuz shit rips if we don't do dis
    		OTKCount--;
    		return;
    	}

    	while(*otkline) { // Loop while we have entries obv
    		if(*otkline == muhotkl) { // Do we need to delete em?
    			last = *otkline; // Get the entry pointur
    			*otkline = last->next; // Set the iterat0r to the next one
    			free_otkline(last); // Free that shit
    			OTKCount--;
    			break;
    		}
    		else {
    			otkline = &(*otkline)->next; // No need, go to the next one
    		}
    	}
    	if(OTKCount <= 0) // Cuz shit rips if we don't do dis
    		moddata_local_variable(otklMDI).ptr = NULL;
    }

    void free_all_otklines(OTKLine *OTKList) {
    	OTKLine *last, **otkline;
    	if(!OTKList)
    		return;

    	otkline = &OTKList; // Hecks so the ->next chain stays intact
    	while(*otkline) { // Loop while we have entries obv
    		last = *otkline; // Get the entry pointur
    		*otkline = last->next; // Set the iterat0r to the next one
    		free_otkline(last); // Free that shit
    		OTKCount--;
    	}
    }

    int match_otkline(Client *client, OTKLine *otkline, char flag) {
    	int gottem = 0; // Equals 2 when we got a solid match
    	char *orig = moddata_local_client(client, otklIdentMDI).ptr;

    	// Check current and stored idents ;]
    	if(match_simple(otkline->user, client->user->username))
    		gottem++;
    	else if(orig && match_simple(otkline->user, orig))
    		gottem++;

    	// Check IP or host based shit ;]
    	if(flag == 'Z' && match_simple(otkline->host, GetIP(client)))
    		gottem++;
    	else if(flag == 'G' && match_simple(otkline->host, client->user->realhost))
    		gottem++;

    	return (gottem == 2 ? 1 : 0);
    }

    void free_otkline(OTKLine *otkline) {
    	if(otkline) {
    		safe_free(otkline->user); // Gotta
    		safe_free(otkline->host); // free
    		safe_free(otkline->raisin); // em
    		safe_free(otkline->setby); // all
    		safe_free(otkline); // lol
    		otkline = NULL; // Just in case imo
    	}
    }

    void otkl_main(Client *client, int parc, const char *parv[], char flag) {
    	char *otxt; // Get command name =]
    	char mask[256]; // Should be enough imo tbh famlalmala
    	char *maskp, *usermask, *hostmask; // Some mask pointers yo
    	char prevmask[USERLEN + HOSTLEN + 1]; // When replacing, show the old one in the notice lel
    	char *p; // For checking mask validity
    	const char *lineby; // When deleted by someone else =]
    	const char *setby; // Set by who (just a nick imo tbh)
    	const char *raisin; // Cleaned reason (like no colours and shit)
    	time_t setat; // UNIX time
    	char gmt[128]; // For a pretty timestamp instead of UNIX time lol
    	int del; // In case it's -id@host shit
    	int repl; // Or replacing one
    	OTKLine *otkline; // To check for existing OTKLines yo
    	OTKLine *newotkl; // For adding one =]
    	OTKLine *OTKList; // For listing that shit
    	Client *acptr; // The mask may also be a nick, so let's store the user pointer for it =]
    	Client *victim, *vnext; // Iterate online users to see if we need to kill any ;];]
    	int i; // Iter8or for reason concaten8ion m8 =]
    	int c; // To send a notice bout not having ne OTKLines
    	int locuser; // Is local user (to store result of MyUser(client) lol)

    	if(IsServer(client) && parc < 6)
    		return;

    	otxt = (flag == 'Z' ? MSG_OZLINE : MSG_OGLINE);
    	locuser = (MyUser(client));

    	// Servers can always use it (in order to sync that shit lol)
    	if(IsUser(client) && (!IsOper(client) || !ValidatePermissionsForPath("otkl", client, NULL, NULL, NULL))) {
    		if(locuser) // We allow remote clients as part of a hack, so only print errors for l0qal users for een bit ;]
    			sendnumeric(client, ERR_NOPRIVILEGES); // Check ur privilege fam
    		return;
    	}

    	if(!strchr("GZ", flag)) { // Should't happen tho tbh
    		if(locuser) // A bunch of shit is silent for servers and rem0te users =]
    			sendnotice(client, "*** Invalid OTKL type '%c'", flag);
    		return;
    	}

    	c = 0;
    	if(BadPtr(parv[1])) { // If first argument is a bad pointer, dump known OTKLines of this type instead
    		if(locuser) { // Only shit out the list if we're talking to a local user here
    			if((OTKList = get_otklines())) { // We gottem list?
    				for(otkline = OTKList; otkline; otkline = otkline->next) { // Checkem
    					if(otkline->flag != flag) // Can only one of the types we carry
    						continue;
    					// Make pwetti~~ timestamp imo
    					*gmt = '\0';
    					short_date(otkline->setat, gmt);
    					sendnotice(client, "*** %s by %s (set at %s GMT) for %s@%s [reason: %s]", otxt, otkline->setby, gmt, otkline->user, otkline->host, otkline->raisin);
    					c++; // For the if below, required for if OGLINE list is empty but OZLINE isn't etc
    				}
    			}
    			if(!c)
    				sendnotice(client, "*** No %sS found", otxt);
    		}
    		return;
    	}

    	if(!strcasecmp(parv[1], "help") || !strcasecmp(parv[1], "halp")) { // If first argument is "halp" or "help"
    		if(!locuser || IsServer(client)) // El silenci0
    			return;
    		dumpit(client, muhHalp); // Return halp string for l0cal users only rly =]
    		return;
    	}

    	// Initialise some shit here =]
    	strlcpy(mask, parv[1], sizeof(mask));
    	maskp = mask;
    	usermask = NULL;
    	hostmask = NULL;
    	p = NULL;
    	lineby = client->name;
    	setby = client->name;
    	raisin = NULL;
    	setat = 0;
    	repl = 0;
    	acptr = NULL;

    	if(IsServer(client)) {
    		setat = atol(parv[2]);
    		setby = parv[3];
    		lineby = parv[4];
    		raisin = parv[5];
    	}

    	if((del = (*maskp == '-')) || *maskp == '+') // Check for +hue@hue and -hue@hue etc
    		maskp++; // Skip that shit

    	// Check for the sanity of the passed mask
    	// Ripped from src/modules/tkl.c with some edits =]
    	if(strchr(maskp, '!')) {
    		if(locuser)
    			sendnotice(client, "*** The mask should be either a nick or of the format ident@host");
    		return;
    	}
    	if(*maskp == ':') {
    		if(locuser)
    			sendnotice(client, "*** Masks cannot start with a ':'");
    		return;
    	}
    	if(strchr(maskp, ' ')) // Found a space in the mask, that should be impossibru ;];]
    		return;

    	// Check if it's a hostmask and legal
    	p = strchr(maskp, '@');
    	if(p) {
    		if((p == maskp) || !p[1]) {
    			if(locuser)
    				sendnotice(client, "*** No (valid) user@host mask specified");
    			return;
    		}
    		usermask = strtok(maskp, "@");
    		hostmask = strtok(NULL, "");
    		if(BadPtr(hostmask)) {
    			if(BadPtr(usermask))
    				return;
    			hostmask = usermask;
    			usermask = "*";
    		}
    		if(*hostmask == ':') {
    			if(locuser)
    				sendnotice(client, "*** For (weird) technical reasons you cannot start the host with a ':'");
    			return;
    		}
    		else if(strchr(hostmask, '/')) {
    			if(locuser)
    				sendnotice(client, "*** CIDR notation for hostmasks is not supported"); // Cbf
    			return;
    		}
    		if(flag == 'Z' && !del) {
    			for(p = hostmask; *p; p++) {
    				if(isalpha(*p) && !isxdigit(*p)) {
    					if(locuser)
    						sendnotice(client, "*** O(G)Z-Lines must be placed at ident@\037IPMASK\037, not ident@\037HOSTMASK\037");
    					return;
    				}
    			}
    		}
    		p = hostmask - 1;
    	}
    	else { // Might be a nick
    		if((acptr = find_user(maskp, NULL))) { // Try to find the user
    			if(!(usermask = moddata_local_client(acptr, otklIdentMDI).ptr))
    				usermask = (acptr->user->username ? acptr->user->username : "*"); // Let's set an ident@host/ip ban lel

    			if(flag == 'Z') { // We got OZLINE
    				hostmask = GetIP(acptr); // So gettem IP lol
    				if(!hostmask) {
    					if(locuser)
    						sendnotice(client, "*** Could not get IP for user '%s'", acptr->name);
    					return;
    				}
    			}
    			else
    				hostmask = acptr->user->realhost; // Get _real_ hostname (not masked/cloaked)
    			p = hostmask - 1;
    		}
    		else { // No such nick found lel
    			if(locuser)
    				sendnumeric(client, ERR_NOSUCHNICK, maskp);
    			return;
    		}
    	}
    	if(!del && ban_too_broad(usermask, hostmask)) { // If we adding but the ban is too broad
    		if(locuser)
    			sendnotice(client, "*** Too broad mask");
    		return;
    	}

    	if(IsUser(client) && !del) { // If a user is adding an OTKLine, we need to concat the reason params =]
    		setat = TStime(); // Get the "setat" time only here for accuracy etc
    		if(!BadPtr(parv[2])) {
    			char rbuf[256]; // Reason buffer, 256 shud b enough xd
    			memset(rbuf, '\0', sizeof(rbuf));
    			for(i = 2; i < parc && parv[i]; i++) { // Start at the "third" arg
    				if(i == 2) // Only for the first time around
    					strlcpy(rbuf, parv[i], sizeof(rbuf)); // cpy() that shit
    				else { // cat() the rest =]
    					strlcat(rbuf, " ", sizeof(rbuf));
    					strlcat(rbuf, parv[i], sizeof(rbuf));
    				}
    			}
    			raisin = (char *)StripControlCodes(rbuf); // No markup pls
    		}
    	}

    	if(!raisin)
    		raisin = DEF_NORAISIN;

    	// Mite need2fow0d the OTKLine to another server (like if the id@ho mask is an online remote nickname instead)
    	if(acptr && !MyUser(acptr)) {
    		// Forward as the user lol (the receiving server will re-parse and rebroadcast em proper OTKLine)
    		sendto_one(acptr->direction, NULL, ":%s %s %s%s :%s", client->name, otxt, (del ? "-" : ""), acptr->name, raisin); // Muh raw command
    		return; // We done for nao
    	}

    	// After all the sanity checks, see if we have an OTKLine alredy
    	otkline = find_otkline(usermask, hostmask, flag); // Attempt to find that shit
    	if(del && !otkline) { // See if the OTKLine even exists when deleting
    		if(IsUser(client))
    			sendnotice(client, "*** %s for %s@%s was not found", otxt, usermask, hostmask);
    		return; // We done
    	}
    	else if(!del && otkline) { // Adding, but already exists kek
    		ircsnprintf(prevmask, sizeof(prevmask), "%s@%s", otkline->user, otkline->host);

    		if(strcasecmp(otkline->user, usermask)) {
    			safe_strdup(otkline->user, usermask);
    			repl = 1;
    		}

    		if(strcasecmp(otkline->host, hostmask)) {
    			safe_strdup(otkline->host, hostmask);
    			repl = 1;
    		}

    		if(strcasecmp(otkline->raisin, raisin)) {
    			safe_strdup(otkline->raisin, raisin);
    			repl = 1;
    		}

    		if(IsUser(client))
    			sendnotice(client, "*** Matching %s for %s@%s already exists%s", otxt, usermask, hostmask, (repl ? ", replacing it" : " (no change detected)"));

    		if(!repl)
    			return; // We done

    		if(strcasecmp(otkline->setby, setby)) {
    			free(otkline->setby);
    			safe_strdup(otkline->setby, setby);
    		}
    		otkline->setat = setat;
    	}
    	else if(!del && !otkline) { // Adding a new one
    		// Es fer b0th servers and users ;]
    		// Allocate/initialise mem0ry for the new entry
    		newotkl = safe_alloc(sizeof(OTKLine));
    		newotkl->flag = flag; // Set flag ('G', 'Z')
    		safe_strdup(newotkl->user, usermask); // Gotta
    		safe_strdup(newotkl->host, hostmask); // dup
    		safe_strdup(newotkl->raisin, raisin); // 'em
    		safe_strdup(newotkl->setby, setby); // all
    		newotkl->setat = setat; // Set timestampus
    		add_otkline(newotkl); // Add to the linked list
    		otkline = newotkl; // Set the main iteration pointer to the new entry ;]
    	}

    	// Propagate the M-Line to other local servers fam (excluding the direction it came from ;])
    	sendto_server(client, 0, 0, NULL, ":%s %s %s%s@%s %ld %s %s :%s", me.id, otxt, (del ? "-" : ""), otkline->user, otkline->host, otkline->setat, otkline->setby, lineby, otkline->raisin);

    	// Also send notices to opers =]
    	// Make pretty setat timestamp first tho
    	*gmt = '\0';
    	short_date(otkline->setat, gmt);

    	if(del && *lineby == '*' && IsServer(client)) { // This condition is true if someone reconnects and hits an OTKLine on a diff server
    		unreal_log(ULOG_INFO, "otkl", "OTKL_MATCH", client, "$otxt by $setby (set at $gmt GMT) for $user@$host was matched, so removing it now [reason: $raisin]",
    			log_data_string("otxt", otxt),
    			log_data_string("setby", otkline->setby),
    			log_data_string("gmt", gmt),
    			log_data_string("user", otkline->user),
    			log_data_string("host", otkline->host),
    			log_data_string("raisin", otkline->raisin)
    		);
    	}
    	else if(repl) {
    		unreal_log(ULOG_INFO, "otkl", "OTKL_REPLACE", client, "$by replaced $otxt for $prevmask with $user@$host [reason: $raisin]",
    			log_data_string("by", (*lineby == '*' ? otkline->setby : lineby)),
    			log_data_string("otxt", otxt),
    			log_data_string("prevmask", prevmask),
    			log_data_string("user", otkline->user),
    			log_data_string("host", otkline->host),
    			log_data_string("raisin", otkline->raisin)
    		);
    	}
    	else {
    		unreal_log(ULOG_INFO, "otkl", (del ? "OTKL_DEL" : "OTKL_ADD"), client, "$otxt $what by $by (set at $gmt GMT) for $user@$host [reason: $raisin]",
    			log_data_string("otxt", otxt),
    			log_data_string("what", (del ? "deleted" : "added")),
    			log_data_string("by", (*lineby == '*' ? otkline->setby : lineby)),
    			log_data_string("gmt", gmt),
    			log_data_string("user", otkline->user),
    			log_data_string("host", otkline->host),
    			log_data_string("raisin", otkline->raisin)
    		);
    	}

    	if(del) // Delete em famamlamlamlmal
    		del_otkline(otkline);
    	else {
    		// Let's kill online (local) users that match this OTKLine =]
    		list_for_each_entry_safe(victim, vnext, &lclient_list, lclient_node) { // Iterate over all local users
    			if(!victim || !MyUser(victim) || !IsUser(victim) || IsULine(victim)) // Just some sanity checks imo
    				continue;

    			if(match_otkline(victim, otkline, flag)) { // Check if victim's user@host matches the OTKLine
    				char banmsg[BUFSIZE]; // Let's construct an exit message similar to G-Lines etc =]
    				ircsnprintf(banmsg, sizeof(banmsg), "User has been permanently banned from %s (%s)", NETWORK_NAME, otkline->raisin);
    				exit_client(victim, NULL, banmsg); // Exit the client y0 =]
    			}
    		}
    	}
    }

    int otkl_hook_serverconnect(Client *client) {
    	// Sync OTKLines fam =]
    	OTKLine *OTKList, *otkline; // Head and iter8or ;];]
    	char *otxt; // Get text OGLINE for 'G' etc
    	if((OTKList = get_otklines())) { // Gettem list
    		for(otkline = OTKList; otkline; otkline = otkline->next) {
    			if(!otkline || !otkline->user) // Sanity check imo ;]
    				continue;
    			otxt = (otkline->flag == 'Z' ? MSG_OZLINE : MSG_OGLINE);
    			// Syntax for servers is a bit different (namely the setat/by args and the : before reason (makes the entire string after be considered one arg ;];])
    			sendto_one(client, NULL, ":%s %s %s@%s %ld %s * :%s", me.id, otxt, otkline->user, otkline->host, otkline->setat, otkline->setby, otkline->raisin); // Muh raw command
    		}
    	}
    	return HOOK_CONTINUE;
    }

    int otkl_hook_pre_localconnect(Client *client) {
    	OTKLine *OTKList, *otkline; // Head and iter8or fam
    	int banned = 0;
    	char *otxt; // Get text OGLINE from 'G' etc
    	char gmt[128];

    	if((OTKList = get_otklines())) { // Check if the list even has entries kek
    		for(otkline = OTKList; otkline; otkline = otkline->next) { // Iter8 em
    			if(match_otkline(client, otkline, otkline->flag)) { // If connecting user matches the OTKLine
    				char banmsg[BUFSIZE]; // Construct the same ban message
    				ircsnprintf(banmsg, sizeof(banmsg), "User has been permanently banned from %s (%s)", NETWORK_NAME, otkline->raisin);
    				otxt = (otkline->flag == 'Z' ? MSG_OZLINE : MSG_OGLINE);
    				exit_client(client, NULL, banmsg); // And kill them =]
    				banned = 1;
    				break; // No need to check any further
    			}
    		}
    	}

    	if(banned) { // We gottem match =]
    		// Make pretty timestamp yet again =3
    		*gmt = '\0';
    		short_date(otkline->setat, gmt);

    		// Notify other servers to delete their copy of the OTKLine, who in turn will also notify their local opers
    		sendto_server(NULL, 0, 0, NULL, ":%s %s -%s@%s %ld %s * :%s", me.id, otxt, otkline->user, otkline->host, otkline->setat, otkline->setby, otkline->raisin); // Muh raw command

    		// And notify online opers about the hit
    		unreal_log(ULOG_INFO, "otkl", "OTKL_MATCH", client, "$otxt by $setby (set at $gmt GMT) for $user@$host was matched, so removing it now [reason: $raisin]",
    			log_data_string("otxt", otxt),
    			log_data_string("setby", otkline->setby),
    			log_data_string("gmt", gmt),
    			log_data_string("user", otkline->user),
    			log_data_string("host", otkline->host),
    			log_data_string("raisin", otkline->raisin)
    		);

    		del_otkline(otkline); // Then actually remove it from the list
    	}
    	else // No match, git dat ident fam ;];]
    		moddata_local_client(client, otklIdentMDI).ptr = strdup(client->user->username);

    	return (banned ? HOOK_DENY : HOOK_CONTINUE);
    }

    // Ripped from src/modules/tkl.c with some edits =]
    int ban_too_broad(char *usermask, char *hostmask) {
    	char *p;
    	int cnt = 0;

    	if(ALLOW_INSANE_BANS)
    		return 0;

    	// Allow things like clone@*, dsfsf@*, etc
    	if(!strchr(usermask, '*') && !strchr(usermask, '?'))
    		return 0;

    	// Need at least 4 non-wildcard/dot chars kek
    	for(p = hostmask; *p; p++) {
    		if(*p != '*' && *p != '.' && *p != '?')
    			cnt++;
    	}

    	if(cnt >= 4)
    		return 0; // We good, so return "not too broad"

    	return 1; // Too broad babe
    }
