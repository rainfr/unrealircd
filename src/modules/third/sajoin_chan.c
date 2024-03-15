    /* Copyright (C) All Rights Reserved
    ** Written by Gottem <support@gottem.nl>
    ** Website: https://gottem.nl/unreal
    ** License: https://gottem.nl/unreal/license
    */

    /*** <<<MODULE MANAGER START>>>
    module {
    	documentation "https://gottem.nl/unreal/man/sajoin_chan";
    	troubleshooting "In case of problems, check the FAQ at https://gottem.nl/unreal/halp or e-mail me at support@gottem.nl";
    	min-unrealircd-version "6.*";
    	//max-unrealircd-version "6.*";
    	post-install-text {
    		"The module is installed, now all you need to do is add a 'loadmodule' line to your config file:";
    		"loadmodule \"third/sajoin_chan\";";
    		"Then /rehash the IRCd.";
    		"For usage information, refer to the module's documentation found at: https://gottem.nl/unreal/man/sajoin_chan";
    	}
    }
    *** <<<MODULE MANAGER END>>>
    */

    // One include for all cross-platform compatibility thangs
    #include "unrealircd.h"

    // Commands to override
    #define OVR_SAJOIN "SAJOIN"

    #define CheckAPIError(apistr, apiobj) \
    	do { \
    		if(!(apiobj)) { \
    			config_error("A critical error occurred on %s for %s: %s", (apistr), MOD_HEADER.name, ModuleGetErrorStr(modinfo->handle)); \
    			return MOD_FAILED; \
    		} \
    	} while(0)

    // Quality fowod declarations
    CMD_OVERRIDE_FUNC(sajoin_chan_override);

    // Dat dere module header
    ModuleHeader MOD_HEADER = {
    	"third/sajoin_chan", // Module name
    	"2.1.0", // Version
    	"Sajoin entire channels into another =]]", // Description
    	"Gottem", // Author
    	"unrealircd-6", // Modversion
    };

    // Initialisation routine (register hooks, commands and modes or create structs etc)
    MOD_INIT() {
    	MARK_AS_GLOBAL_MODULE(modinfo);
    	return MOD_SUCCESS;
    }

    // Actually load the module here (also command overrides as they may not exist in MOD_INIT yet)
    MOD_LOAD() {
    	CheckAPIError("CommandOverrideAdd(SAJOIN)", CommandOverrideAdd(modinfo->handle, OVR_SAJOIN, 0, sajoin_chan_override));
    	return MOD_SUCCESS; // We good
    }

    // Called on unload/rehash obv
    MOD_UNLOAD() {
    	return MOD_SUCCESS; // We good
    }

    // Now for the actual override
    CMD_OVERRIDE_FUNC(sajoin_chan_override) {
    	// Gets args: CommandOverride *ovr, Client *client, MessageTag *recv_mtags, int parc, char *parv[]
    	const char *victimchan;
    	const char *targets;
    	Channel *channel; // Pointer for victim channel
    	Client *acptr; // Client pointer from the victim channel
    	Member *member; // Channels work internally with "members" and "memberships" ;];]

    	if(BadPtr(parv[1]) || BadPtr(parv[2]) || parv[1][0] != '#' || !MyUser(client) || !ValidatePermissionsForPath("sajoin", client, NULL, NULL, NULL)) {
    		CallCommandOverride(ovr, client, recv_mtags, parc, parv); // Run original function yo
    		return;
    	}

    	victimchan = parv[1];
    	targets = parv[2];

    	if(!(channel = find_channel(victimchan))) {
    		sendnumeric(client, ERR_NOSUCHCHANNEL, victimchan);
    		return;
    	}

    	for(member = channel->members; member; member = member->next) {
    		acptr = member->client;
    		if(IsULine(acptr)) // Let's skip these ;];]
    			continue;

    		const char *newparv[4];
    		newparv[0] = NULL;
    		newparv[1] = acptr->name;
    		newparv[2] = targets;
    		newparv[3] = NULL;

    		do_cmd(client, NULL, OVR_SAJOIN, 3, newparv);
    	}

    	return; // Stop processing yo
    }
