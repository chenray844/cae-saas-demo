# -*- coding: utf-8 -*-

import logging
import sys
from pathlib import Path
import subprocess
import os
import shutil
import datetime

import colorlog
import click

root = Path(__file__).parent.absolute()


def makeLogger(name, level=logging.INFO, stream=sys.stdout):
    logger = logging.getLogger(name=name)

    # fmt = logging.Formatter('%(name)s : [%(asctime)s] %(message)s')
    fmt = colorlog.ColoredFormatter(
        '%(name)s: %(white)s%(asctime)s%(reset)s | %(log_color)s%(levelname)s%(reset)s | %(log_color)s%(message)s%(reset)s')

    stdout = logging.StreamHandler(stream=stream)
    stdout.setLevel(level=level)
    stdout.setFormatter(fmt)

    logger.addHandler(stdout)
    logger.setLevel(level=level)

    return logger


logger = makeLogger("CAE_SAAS", level=logging.DEBUG, stream=sys.stdout)


@click.group()
@click.pass_context
def cli(ctx):
    pass


@cli.command()
@click.pass_context
def deploy(ctx):
    logger.info('starting build & package & deploy ...')

    wasmDir = root/'cae-demo'/'src'/'wasm'/'build'
    cmd = ['cmake', '--build', '.', '--parallel', f'{os.cpu_count()}']
    logger.info(f'start wasm build : {cmd} at {wasmDir}')
    subprocess.run(cmd, cwd=wasmDir)

    ngDir = root/'cae-demo'
    cmd = ['ng', 'build']
    logger.info(f'start building angular app : {cmd} at {ngDir}')
    subprocess.run(cmd, cwd=ngDir)

    publicDir = root/'docs'
    if publicDir.exists():
        shutil.rmtree(publicDir)
    # publicDir.mkdir(parents=True, exist_ok=True)

    logger.info(f'coping to {publicDir}')
    shutil.copytree(ngDir/'dist'/'cae-demo', publicDir)

    cmd = ['git', 'add', '.']
    logger.info(f'{cmd} at {root}')
    subprocess.run(cmd, cwd=root)

    cmd = ['git', 'commit', '-m',
           f'deploy to repo at {datetime.datetime.now()}']
    logger.info(f'{cmd} at {root}')
    subprocess.run(cmd, cwd=root)

    cmd = ['git', 'push', 'origin', 'main']
    logger.info(f'{cmd} at {root}')
    subprocess.run(cmd, cwd=root)


if __name__ == '__main__':
    cli(obj={})
