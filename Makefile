.PHONY: test demo serve clean

test:
	. .venv/bin/activate && pytest -q

demo:
	. .venv/bin/activate && python -m pipeline.build --synth --rows 500 --cols 500 --out data/out

serve:
	. .venv/bin/activate && python relay/app.py data/out

clean:
	rm -f data/out/*.npy data/out/*.png data/out/routes.json data/out/tiles/*.bmp
