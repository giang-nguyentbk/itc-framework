#include <stdint.h>

#define MODULE_XYZ_SIG_BASE                     0x10000000
#define MODULE_XYZ_INTERFACE_ABC_SIG_BASE       (MODULE_XYZ_SIG_BASE + 0x5000)



// Request messages.
#define MODULE_XYZ_INTERFACE_ABC_SETUP1_REQ     (MODULE_XYZ_INTERFACE_ABC_SIG_BASE + 0x1)
struct InterfaceAbcModuleXyzSetup1ReqS {
        uint32_t sigNo;
        uint32_t serverId;
        uint32_t clientId;
        uint32_t procedureId;
        uint32_t pattern;
        uint32_t param1;
	char     large_pl[1];
};

#define MODULE_XYZ_INTERFACE_ABC_ACTIVATE_REQ   (MODULE_XYZ_INTERFACE_ABC_SIG_BASE + 0x2)
struct InterfaceAbcModuleXyzActivateReqS {
        uint32_t sigNo;
        uint32_t serverId;
        uint32_t clientId;
        uint32_t procedureId;
        uint32_t temperature;
        uint32_t speed;
	char     large_pl[1];
};

#define MODULE_XYZ_INTERFACE_ABC_DEACTIVATE_REQ (MODULE_XYZ_INTERFACE_ABC_SIG_BASE + 0x3)
struct InterfaceAbcModuleXyzDeactivateReqS {
        uint32_t sigNo;
        uint32_t serverId;
        uint32_t clientId;
        uint32_t procedureId;
        uint32_t temperature;
        uint32_t speed;
	char     large_pl[1];
};

#define MODULE_XYZ_INTERFACE_ABC_RELEASE_REQ    (MODULE_XYZ_INTERFACE_ABC_SIG_BASE + 0x4)
struct InterfaceAbcModuleXyzReleaseReqS {
        uint32_t sigNo;
        uint32_t serverId;
        uint32_t clientId;
        uint32_t procedureId;
	char     large_pl[1];
};

// Confirm messages to acknowledge that we have received Request messages.
#define MODULE_XYZ_INTERFACE_ABC_SETUP1_CFM     (MODULE_XYZ_INTERFACE_ABC_SIG_BASE + 0x5)
struct InterfaceAbcModuleXyzSetup1CfmS {
        uint32_t sigNo;
        uint32_t serverId;
        uint32_t clientId;
        uint32_t procedureId;
	char     large_pl[1];
};

#define MODULE_XYZ_INTERFACE_ABC_ACTIVATE_CFM   (MODULE_XYZ_INTERFACE_ABC_SIG_BASE + 0x6)
struct InterfaceAbcModuleXyzActivateCfmS {
        uint32_t sigNo;
        uint32_t serverId;
        uint32_t clientId;
        uint32_t procedureId;
	char     large_pl[1];
};


#define MODULE_XYZ_INTERFACE_ABC_DEACTIVATE_CFM (MODULE_XYZ_INTERFACE_ABC_SIG_BASE + 0x7)
struct InterfaceAbcModuleXyzDeactivateCfmS {
        uint32_t sigNo;
        uint32_t serverId;
        uint32_t clientId;
        uint32_t procedureId;
	char     large_pl[1];
};

#define MODULE_XYZ_INTERFACE_ABC_RELEASE_CFM    (MODULE_XYZ_INTERFACE_ABC_SIG_BASE + 0x8)
struct InterfaceAbcModuleXyzReleaseCfmS {
        uint32_t sigNo;
        uint32_t serverId;
        uint32_t clientId;
        uint32_t procedureId;
	char     large_pl[1];
};


// Indication messages to inform that we have carried out procedures that need some time to be done.
// Such as configure HW, or change temperature, capture or calibration some signals.
#define MODULE_XYZ_INTERFACE_ABC_ACTIVATE_IND   (MODULE_XYZ_INTERFACE_ABC_SIG_BASE + 0x9)
struct InterfaceAbcModuleXyzActivateIndS {
        uint32_t sigNo;
        uint32_t serverId;
        uint32_t clientId;
        uint32_t procedureId;
        uint32_t temperature;
        uint32_t speed;
	char     large_pl[1];
};

#define MODULE_XYZ_INTERFACE_ABC_DEACTIVATE_IND (MODULE_XYZ_INTERFACE_ABC_SIG_BASE + 0xA)
struct InterfaceAbcModuleXyzDeactivateIndS {
        uint32_t sigNo;
        uint32_t serverId;
        uint32_t clientId;
        uint32_t procedureId;
        uint32_t temperature;
        uint32_t speed;
	char     large_pl[1];
};

union itc_msg {
        uint32_t msgNo;

        struct InterfaceAbcModuleXyzSetup1ReqS          InterfaceAbcModuleXyzSetup1Req;
        struct InterfaceAbcModuleXyzActivateReqS        InterfaceAbcModuleXyzActivateReq;
        struct InterfaceAbcModuleXyzDeactivateReqS      InterfaceAbcModuleXyzDeactivateReq;
        struct InterfaceAbcModuleXyzReleaseReqS         InterfaceAbcModuleXyzReleaseReq;
        struct InterfaceAbcModuleXyzSetup1CfmS          InterfaceAbcModuleXyzSetup1Cfm;
        struct InterfaceAbcModuleXyzActivateCfmS        InterfaceAbcModuleXyzActivateCfm;
        struct InterfaceAbcModuleXyzDeactivateCfmS      InterfaceAbcModuleXyzDeactivateCfm;
        struct InterfaceAbcModuleXyzReleaseCfmS         InterfaceAbcModuleXyzReleaseCfm;
        struct InterfaceAbcModuleXyzActivateIndS        InterfaceAbcModuleXyzActivateInd;
        struct InterfaceAbcModuleXyzDeactivateIndS      InterfaceAbcModuleXyzDeactivateInd;
};