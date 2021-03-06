import os
import logging
from .config import Config


class LogConfig(Config):
    def __init__(self):
        self.name = "rover6"
        self.level = logging.DEBUG
        self.file_name = "rover6"
        self.suffix = "%Y-%m-%d.log"
        self.format = "%(levelname)s\t%(asctime)s\t[%(name)s, %(filename)s:%(lineno)d]\t%(message)s"

        super(LogConfig, self).__init__("logging.yaml")

        self.dir = os.path.join(self.base_dir, "logs")
        if not os.path.isdir(self.dir):
            os.makedirs(self.dir)
        self.path = os.path.join(self.dir, self.file_name)

    def to_dict(self):
        return {
            "name": self.name,
            "level": self.level,
            "file_name": self.file_name,
            "suffix": self.suffix,
            "format": self.format,
            "path": self.path,
            "dir": self.dir
        }
