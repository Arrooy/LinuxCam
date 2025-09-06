#!/usr/bin/env python3
"""
Script to extract the emap matrix from an inswapper ONNX model.
This matrix is needed for transforming ArcFace embeddings to inswapper-compatible space.
"""

import os
import sys

import numpy as np

import onnx
from onnx import numpy_helper


def extract_emap_matrix(inswapper_model_path, output_path=None):
    """
    Extract the emap matrix from an inswapper ONNX model.

    Args:
        inswapper_model_path: Path to the inswapper ONNX model
        output_path: Path where to save the emap matrix (optional)

    Returns:
        numpy array containing the emap matrix
    """
    try:
        # Load the ONNX model
        print(f"Loading ONNX model from: {inswapper_model_path}")
        model = onnx.load(inswapper_model_path)
        graph = model.graph

        # Extract the emap matrix (should be the last initializer)
        emap = numpy_helper.to_array(graph.initializer[-1])

        print(f"Extracted emap matrix shape: {emap.shape}")
        print(f"Emap matrix dtype: {emap.dtype}")

        # Verify it's the expected size (512x512)
        if emap.shape != (512, 512):
            print(f"Warning: Expected emap matrix shape (512, 512), got {emap.shape}")
            print("This might not be the correct matrix for embedding transformation")

        # If no output path specified, create one based on input path
        if output_path is None:
            output_path = inswapper_model_path + ".emap"

        # Save as binary file (float32)
        emap_float32 = emap.astype(np.float32)
        print(f"Saving emap matrix to: {output_path}")
        emap_float32.tofile(output_path)

        # Also save as readable text file for inspection
        text_output_path = output_path + ".txt"
        print(f"Saving emap matrix (text format) to: {text_output_path}")
        np.savetxt(text_output_path, emap_float32, fmt="%.6f")

        print("Emap matrix extraction completed successfully!")
        print(f"To use with LinuxCam, place the .emap file alongside your inswapper model")

        return emap_float32

    except Exception as e:
        print(f"Error extracting emap matrix: {e}")
        return None


def main():
    if len(sys.argv) < 2:
        print("Usage: python extract_emap_matrix.py <inswapper_model_path> [output_path]")
        print("Example: python extract_emap_matrix.py models/inswapper_128.onnx")
        sys.exit(1)

    inswapper_model_path = sys.argv[1]
    output_path = sys.argv[2] if len(sys.argv) > 2 else None

    if not os.path.exists(inswapper_model_path):
        print(f"Error: Model file not found: {inswapper_model_path}")
        sys.exit(1)

    emap = extract_emap_matrix(inswapper_model_path, output_path)

    if emap is not None:
        print("\nEmap matrix statistics:")
        print(f"  Mean: {np.mean(emap):.6f}")
        print(f"  Std:  {np.std(emap):.6f}")
        print(f"  Min:  {np.min(emap):.6f}")
        print(f"  Max:  {np.max(emap):.6f}")
        print(f"  Norm: {np.linalg.norm(emap):.6f}")
    else:
        sys.exit(1)


if __name__ == "__main__":
    main()
