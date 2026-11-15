"""Production entry point for Render: dependencies are installed during the build."""
import os
from pathlib import Path

from iniciar import DATA_FILES, prepare_data

ROOT = Path(__file__).resolve().parent


def application():
    directory = Path(os.environ.get('RUTA_DATA_DIR', str(ROOT / 'assets' / 'jezero')))
    if not directory.is_absolute():
        directory = ROOT / directory
    if not all((directory / name).is_file() for name in DATA_FILES):
        raise ValueError('Falta el paquete de Jezero. Despliega la rama con assets/jezero completo.')
    return prepare_data(directory)


def main():
    from waitress import serve
    port = int(os.environ.get('PORT', '10000'))
    if not 1 <= port <= 65535:
        raise ValueError('PORT debe estar entre 1 y 65535.')
    # Serialize route calculations to bound memory on the 512 MB free compute plan.
    serve(application(), host='0.0.0.0', port=port, threads=1)


if __name__ == '__main__':
    main()
