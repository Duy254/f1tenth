# F1Tenth

## Run code

If you have pretrained model just go to `evaluation`

### Prepare dataset

1. Download ros bags
2. Use `Preprocess_Bag.ipynb` to export images frmo bags
3. Create list of files:
```
cd DIRECTORY_WITH_EXPORTED_IMAGES
ls -1 > images_list.csv
```

### Training

```
python model.py --list images_list.csv --output logs --dataset dataset001/images/
```

### Evaluation

```
python evaluate.py --list images_list.csv --output logs --dataset dataset001/images/ --model ./logs/logs_1529490181/weights.0040-0.036.hdf5
```

## Datasets

**Dataset001**

<a href="http://www.youtube.com/watch?feature=player_embedded&v=OgwDYOZj3LQ
" target="_blank"><img src="http://img.youtube.com/vi/OgwDYOZj3LQ/0.jpg"
alt="Dataset 001" width="240" height="180" border="10" /></a>

[Download Dataset001](https://archive.org/details/20180603152844)

## Team members
- [Karol Majek](https://karolmajek.pl)
- [Maciej Dziubiński](https://www.linkedin.com/in/maciej-dziubinski/)
- [Łukasz Sztyber](https://www.linkedin.com/in/lukaszsztyber/)
- [Artur Olszak](https://www.linkedin.com/in/aolszak/)

## More info
- [Deep Drive PL Blog](https://deepdrive.pl)
- [F1/10](http://f1tenth.org/)
- [Deep Drive VLOG 0007 PL](https://youtu.be/7u3DkrIT04s)
- [Blog post](https://deepdrive.pl/samochod-autonomiczny-w-skali/)
