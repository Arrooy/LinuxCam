"""
Download ONNX models from Google Drive using service account credentials.

This script downloads model files from a shared Google Drive folder using
the Google Drive API v3 with service account authentication.
"""

import os
import io
import sys
import tempfile
from typing import List, Dict
from google.oauth2 import service_account
from googleapiclient.discovery import build
from googleapiclient.http import MediaIoBaseDownload

# Configuration
FOLDER_ID = "18OdNkL0qTq7JhtLYLF8-IoBZtT70w9BW"
OUTPUT_DIR = "models"
DRIVE_API_SCOPES = ["https://www.googleapis.com/auth/drive.readonly"]


def setup_output_directory() -> None:
    """Create the output directory if it doesn't exist."""
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    print(f"Output directory ready: {OUTPUT_DIR}")


def load_service_account_credentials() -> service_account.Credentials:
    """
    Load Google Drive service account credentials from environment variable.

    Returns:
        service_account.Credentials: Authenticated credentials object

    Raises:
        SystemExit: If GDRIVE_CREDENTIALS environment variable is missing
    """
    creds_json = os.environ.get("GDRIVE_CREDENTIALS")
    if not creds_json:
        print("ERROR: Missing GDRIVE_CREDENTIALS environment variable", file=sys.stderr)
        sys.exit(1)

    # Write credentials to temporary file for authentication
    with tempfile.NamedTemporaryFile(delete=False, mode="w", suffix=".json") as f:
        f.write(creds_json)
        creds_file = f.name

    try:
        creds = service_account.Credentials.from_service_account_file(creds_file, scopes=DRIVE_API_SCOPES)
        print("Service account credentials loaded successfully")
        return creds
    finally:
        # Clean up temporary credentials file
        os.unlink(creds_file)


def get_drive_service(credentials: service_account.Credentials):
    """Build and return Google Drive API service."""
    return build("drive", "v3", credentials=credentials)


def list_files_in_folder(service, folder_id: str) -> List[Dict[str, str]]:
    """
    List all files in the specified Google Drive folder.

    Args:
        service: Google Drive API service object
        folder_id: Google Drive folder ID

    Returns:
        List of file dictionaries with 'id' and 'name' keys

    Raises:
        SystemExit: If no files are found in the folder
    """
    print(f"Listing files in Google Drive folder: {folder_id}")

    results = service.files().list(q=f"'{folder_id}' in parents and trashed=false", fields="files(id, name)").execute()

    files = results.get("files", [])
    if not files:
        print("WARNING: No files found in the specified folder", file=sys.stderr)
        sys.exit(1)

    print(f"Found {len(files)} files to download")
    return files


def download_file(service, file_id: str, file_name: str, output_dir: str) -> None:
    """
    Download a single file from Google Drive.

    Args:
        service: Google Drive API service object
        file_id: Google Drive file ID
        file_name: Name of the file to download
        output_dir: Directory to save the downloaded file
    """
    print(f"Downloading: {file_name}")

    request = service.files().get_media(fileId=file_id)
    file_path = os.path.join(output_dir, file_name)

    with io.FileIO(file_path, "wb") as fh:
        downloader = MediaIoBaseDownload(fh, request)
        done = False
        while not done:
            status, done = downloader.next_chunk()
            if status:
                progress = int(status.progress() * 100)
                print(f"  Progress: {progress}%")

    print(f"  Completed: {file_name}")


def main() -> None:
    """Main function to orchestrate the model download process."""
    print("Starting ONNX model download from Google Drive")

    # Setup
    setup_output_directory()
    credentials = load_service_account_credentials()
    service = get_drive_service(credentials)

    # Download all files
    files = list_files_in_folder(service, FOLDER_ID)

    for file_info in files:
        file_id = file_info["id"]
        file_name = file_info["name"]
        download_file(service, file_id, file_name, OUTPUT_DIR)

    print(f"SUCCESS: All {len(files)} models downloaded to: {OUTPUT_DIR}")


if __name__ == "__main__":
    main()
