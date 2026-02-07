#pragma once

// =============================================================================
// Conditional includes for CommonLibSSE types
// In test environment, use stubs. In production, use real headers.
// =============================================================================

#ifdef TEST_ENVIRONMENT
    // Use test stubs
    #include "../../Tests/TestStubs.h"
#else
    // Use real CommonLibSSE headers
    #include <RE/N/NiTransform.h>
    #include <RE/N/NiPoint3.h>
    #include <RE/N/NiMatrix3.h>
    #include <RE/F/FormTypes.h>
    #include <RE/T/TESForm.h>
    #include <RE/T/TESObjectREFR.h>
    #include <RE/T/TESObjectCELL.h>
    #include <RE/T/TESModel.h>
    #include <RE/T/TESDataHandler.h>
#endif
