/** @file
  Boot entry protocol implementation of Reset NVRAM boot picker entry.

  Copyright (c) 2022, Mike Beaton. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-3-Clause
**/

#include <Guid/AppleVariable.h>

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/OcDeviceMiscLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>

#include <Protocol/OcBootEntry.h>

#define OC_MENU_RESET_NVRAM_ENTRY  "Reset NVRAM"

STATIC BOOLEAN  mIsNative     = FALSE;
STATIC BOOLEAN  mPreserveBoot = FALSE;

STATIC OC_PICKER_ENTRY  mResetNvramBootEntries[1] = {
  {
    .Id            = "reset_nvram",
    .Name          = OC_MENU_RESET_NVRAM_ENTRY,
    .Path          = NULL,
    .Arguments     = NULL,
    .Flavour       = OC_FLAVOUR_RESET_NVRAM,
    .Auxiliary     = TRUE,
    .Tool          = FALSE,
    .TextMode      = FALSE,
    .RealPath      = FALSE,
    .SystemAction  = NULL, ///< overridden in set up
    .ActionConfig  = &mPreserveBoot,
    .AudioBasePath = OC_VOICE_OVER_AUDIO_FILE_RESET_NVRAM,
    .AudioBaseType = OC_VOICE_OVER_AUDIO_BASE_TYPE_OPEN_CORE
  }
};

STATIC
EFI_STATUS
EFIAPI
ResetNvramGetBootEntries (
  IN           OC_PICKER_CONTEXT  *PickerContext,
  IN     CONST EFI_HANDLE         Device,
  OUT       OC_PICKER_ENTRY       **Entries,
  OUT       UINTN                 *NumEntries
  )
{
  //
  // Custom entries only.
  //
  if (Device != NULL) {
    return EFI_NOT_FOUND;
  }

  DEBUG ((DEBUG_INFO, "BEP: Adding Reset NVRAM entry, preserve boot %u, native %u\n", mPreserveBoot, mIsNative));

  *Entries    = mResetNvramBootEntries;
  *NumEntries = ARRAY_SIZE (mResetNvramBootEntries);

  return EFI_SUCCESS;
}

STATIC
OC_BOOT_ENTRY_PROTOCOL
  mResetNvramBootEntryProtocol = {
  OC_BOOT_ENTRY_PROTOCOL_REVISION,
  ResetNvramGetBootEntries,
  NULL
};

//
// Use OpenCore NVRAM reset, potentially preserving Boot#### entries.
// Remarks: We don't need to use LoadOptions here, since it is referring to
// a variable we have access to, but we want to confirm that the system works.
//
STATIC
EFI_STATUS
InternalSystemActionResetNvram (
  IN     VOID  *Config
  )
{
  ASSERT (Config == &mPreserveBoot);
  return OcResetNvram (*((BOOLEAN *)Config));
}

//
// Request native NVRAM reset, potentially including NVRAM garbage collection, etc. on real Mac.
//
STATIC
EFI_STATUS
InternalSystemActionResetNvramNative (
  IN     VOID  *Config
  )
{
  UINT8  ResetNVRam = 1;

  //
  // Any size, any value for this variable will cause a reset on supported firmware.
  //
  gRT->SetVariable (
         APPLE_RESET_NVRAM_VARIABLE_NAME,
         &gAppleBootVariableGuid,
         EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS | EFI_VARIABLE_NON_VOLATILE,
         sizeof (ResetNVRam),
         &ResetNVRam
         );

  DirectResetCold ();

  return EFI_DEVICE_ERROR;
}

EFI_STATUS
EFIAPI
UefiMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS                 Status;
  EFI_LOADED_IMAGE_PROTOCOL  *LoadedImage;
  OC_FLEX_ARRAY              *ParsedLoadOptions;

  Status = gBS->HandleProtocol (
                  ImageHandle,
                  &gEfiLoadedImageProtocolGuid,
                  (VOID **)&LoadedImage
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = OcParseLoadOptions (LoadedImage, &ParsedLoadOptions);
  if (!EFI_ERROR (Status)) {
    mPreserveBoot = OcHasParsedVar (ParsedLoadOptions, L"--preserve-boot", TRUE);
    mIsNative     = OcHasParsedVar (ParsedLoadOptions, L"--native", TRUE);

    OcFlexArrayFree (&ParsedLoadOptions);
  } else {
    ASSERT (ParsedLoadOptions == NULL);

    if (Status != EFI_NOT_FOUND) {
      return Status;
    }
  }

  mResetNvramBootEntries[0].SystemAction =
    mIsNative
      ? InternalSystemActionResetNvramNative
      : InternalSystemActionResetNvram;

  if (mIsNative && mPreserveBoot) {
    DEBUG ((DEBUG_WARN, "BEP: ResetNvram %s is ignored due to %s!\n", L"--preserve-boot", L"--native"));
  }

  Status = gBS->InstallMultipleProtocolInterfaces (
                  &ImageHandle,
                  &gOcBootEntryProtocolGuid,
                  &mResetNvramBootEntryProtocol,
                  NULL
                  );
  ASSERT_EFI_ERROR (Status);

  return Status;
}
