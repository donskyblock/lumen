
# Juice WRLD API Source for Lumen

A Lumen source extension that uses the Juice WRLD API to search the
catalogue and resolve audio and artwork.

## Features

- Searches released, unreleased, and unsurfaced catalogue categories.
- Excludes recording-session entries.
- Resolves playback URLs through the API's file endpoint.
- Retrieves cover artwork through the API.
- Groups tracks under the artist Juice WRLD.
- Includes basic recommendations based on era and album metadata.
- Requires no additional Python packages.

## Installation

1. Create a folder named `juicewrld` in your Lumen extensions directory.
2. Place `main.py`, `plugin.json`, and `README.md` inside it.
3. Make sure Python 3 is installed and available as `python3`.
4. Ensure the extension can access the internet.
5. Restart Lumen or reload its extensions.

## API

Website:
https://juicewrldapi.com/files

API base:
https://juicewrldapi.com/juicewrld

## Notes

- The API is community-maintained. Its endpoints and catalogue may change.
- Some entries may not have playable audio or artwork.
- Only catalogue entries with a usable file path are included in search.
- This extension does not bundle audio files.
- Use content only where you have the appropriate rights.
