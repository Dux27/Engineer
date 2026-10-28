import random
import shutil
import tempfile
import zipfile
from pathlib import Path
from urllib.request import urlopen

from tqdm import tqdm

URL = "https://uni-siegen.sciebo.de/s/HGdUkoNlW1Ub0Gx/download"
DATA_DIR = Path(__file__).resolve().parent.parent / "wesad_dataset"
SEED = 27

with tempfile.TemporaryDirectory() as temp:
    archive = Path(temp) / "WESAD.zip"

    with urlopen(URL) as response:
        total = int(response.headers.get("Content-Length", 0))

        with (
            archive.open("wb") as file,
            tqdm(
                total=total or None,
                unit="B",
                unit_scale=True,
                unit_divisor=1024,
                desc="Downloading WESAD",
            ) as progress,
        ):
            while chunk := response.read(1024 * 1024):
                file.write(chunk)
                progress.update(len(chunk))

    with zipfile.ZipFile(archive) as zf:
        files = sorted(
            (
                f
                for f in zf.namelist()
                if f.endswith(".pkl") and Path(f).stem not in {"S1", "S12"}
            ),
            key=lambda f: int(Path(f).stem[1:]),
        )

        if len(files) != 15:
            raise ValueError(f"Expected 15 subjects, found {len(files)}")

        random.Random(SEED).shuffle(files)

        splits = {
            "train": files[:12],
            "val": files[12:14],
            "test": files[14:],
        }
        assigned = [subject for subjects in splits.values() for subject in subjects]
        assert len(assigned) == len(set(assigned)) == len(files)

        for split, subjects in splits.items():
            destination = DATA_DIR / split
            destination.mkdir(parents=True, exist_ok=True)

            for subject in subjects:
                existing = list(DATA_DIR.glob(f"*/{Path(subject).name}"))
                if existing:
                    # Preserve existing split assignments and file contents.
                    print(f"Preserving existing subject location: {existing}")
                    continue
                with (
                    zf.open(subject) as src, 
                    (destination / Path(subject).name).open("wb") as dst
                ):
                    shutil.copyfileobj(src, dst)

            print(f"{split}: {[Path(s).stem for s in subjects]}")

print("Done!")
