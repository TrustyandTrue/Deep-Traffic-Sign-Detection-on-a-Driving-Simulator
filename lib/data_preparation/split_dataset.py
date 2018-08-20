import os
import numpy as np
from numpy.core.defchararray import replace
import math

# TODO import from fast_rcnn cfg
class DatasetSplit:
    def __init__(self, path_to_annotations='Annotations', path_to_imagesets='ImageSets'):
	devkit_path = os.path.join('..','..','data', 'traffic_devkit')
	data_path = os.path.join(devkit_path, 'data')
        self.path_to_annotations = os.path.join(data_path, path_to_annotations)
        self.path_to_imagesets = os.path.join(data_path, path_to_imagesets)

    def get_filenames(self, file_ext='.xml'):
        """ Helper function which gets the filename identifiers (exluding the file extension) from a directory

                Args:
                    path_to_dataset (string): Path to directory with the files
                    file_ext (string): File extension to be spliced out of filename
                Returns:
                    ndarray with the files identifiers
        """
        files = os.listdir(self.path_to_annotations)
        arr = np.array(files)
        result = replace(arr, file_ext, '')

        return result


    def split_to(self, data, train=0.8, val=0.2):
        """Function splits data into train, test and validation sets by randomly sampling from data
        and the following rules:

        1. Split data into (intermediary) training set and testing set
        2. Split the intermediary training set into final training set and validation set

        Note: Size of test set is implicitly because of sum-to-one constraint.

            Args:
                data (ndarray): The data to be split. It must have shape (n,) where n is the number of data points.
                train (double): The size of the training set in per mille. Default value is 0.8
                val (double): The size of the validation set in per mille. Default value is 0.2

            Returns:
                Three ndarrays, which are train_set, test_set and validation_set

        """
        index_array = np.arange(len(data))
        np.random.shuffle(index_array)

        train_idx = int(math.floor(train * len(data)))
        test_idx = int(math.ceil((1-train) * len(data)))

        train_temp = data[index_array[:train_idx]]
        test_set = data[index_array[-test_idx:]]

        assert (len([x for x in train_temp if x in test_set]) == 0)

        index_array = np.arange(len(train_temp))
        np.random.shuffle(index_array)

        train_idx = int(math.floor((1-val) * len(train_temp)))
        val_idx = int(math.ceil(val * len(train_temp)))

        train_set = train_temp[index_array[:train_idx]]
        val_set = train_temp[index_array[-val_idx:]]

        assert(len(test_set) + len(val_set) + len(train_set) == len(data))
        assert(len([x for x in train_set if x in val_set]) == 0)

        return train_set, test_set, val_set

    def save_to_files(self, train, test, val):
        """ Function to save the train, test and validation splits into the corresponding textfiles.

                    Args:
                        path (string): Path to directory containing the textfiles.
                        train (ndarray): ndarray containing training datapoints to be saved to textfile
                        test (ndarray): ndarray containing testing datapoints to be saved to textfile
                        val (ndarray): ndarray containing validation datapoints to be saved to textfile
                    Returns:
                        void
        """
        np.savetxt(os.path.join(self.path_to_imagesets, 'train.txt'), train, fmt='%s', delimiter=' ', newline='\n')
        np.savetxt(os.path.join(self.path_to_imagesets, 'test.txt'), test, fmt='%s', delimiter=' ', newline='\n')
        np.savetxt(os.path.join(self.path_to_imagesets, 'val.txt'), val, fmt='%s', delimiter=' ', newline='\n')


if __name__ == '__main__':
    ds = DatasetSplit()
    data = ds.get_filenames()
    train_set, test_set, val_set = ds.split_to(data)
    ds.save_to_files(train_set, test_set, val_set)
