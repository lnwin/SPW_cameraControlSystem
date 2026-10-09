"""setup.py — spwcam Python package installer

The native DLL is NOT bundled inside the Python package: it must be loaded from
the SDK `bin` directory together with all of its runtime dependencies.
Either keep this package inside the SDK tree (<sdk>/python/spwcam, resolved
automatically) or set the SPWCAM_SDK_BIN environment variable.
"""

from setuptools import setup, find_packages

setup(
    name             = "spwcam",
    version          = "2.0.0",
    description      = "SPWater Camera SDK Python wrapper",
    python_requires  = ">=3.8",
    packages         = find_packages(),
    extras_require   = {"numpy": ["numpy>=1.21"]},
    classifiers      = [
        "Programming Language :: Python :: 3",
        "Operating System :: Microsoft :: Windows",
    ],
)
