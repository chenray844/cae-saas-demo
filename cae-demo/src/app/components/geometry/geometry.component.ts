import { Component, OnInit, OnDestroy, AfterViewInit } from '@angular/core';
import { MenuItem } from 'primeng/api';
import { fromEvent } from 'rxjs';
declare var OccApp: any;

@Component({
  selector: 'app-geometry',
  templateUrl: './geometry.component.html',
  styleUrls: ['./geometry.component.css']
})
export class GeometryComponent implements OnInit, OnDestroy, AfterViewInit {
  dockItems: MenuItem[] | undefined;
  selectedFile: File | undefined;
  fileformat: string = '.stp,.step,.STP,.STEP';
  OccViewer: any | undefined;

  constructor() {
    const self = this;

    fromEvent(window, 'resize').subscribe(() => {
      let sizeX = Math.min(window.innerWidth, window.screen.availWidth);
      let sizeY = Math.min(window.innerHeight, window.screen.availHeight);

      const canvas = <HTMLCanvasElement>document.getElementById('canvas');
      canvas.style.width = `${sizeX}px`;
      canvas.style.height = `${sizeY}px`;

      let ratio = window.devicePixelRatio || 1;
      canvas.width = sizeX * ratio;
      canvas.height = sizeY * ratio;
    });
  }

  ngOnInit(): void {
    const self = this;

    self.dockItems = [
      {
        label: 'CAD',
        icon: 'assets/cad-icon.svg',
        command: (event: any) => {
          self.onOpenCAD(event);
        },
      }
    ]
  }

  ngOnDestroy(): void {
    const self = this;
  }

  ngAfterViewInit(): void {
    const self = this;
    self.initOCCViewer();

  }

  private async initOCCViewer() {
    const self = this;

    const canvas = document.createElement('canvas');
    canvas.setAttribute('id', 'canvas');
    canvas.setAttribute('class', 'canvas');
    canvas.oncontextmenu = (event: any) => {
      event.preventDefault();
      event.stopPropagation();
    };
    canvas.tabIndex = -1;
    canvas.onclick = () => { canvas.focus(); };
    const container = <HTMLDivElement>document.getElementById('occt_container');
    container.appendChild(canvas);

    const config = {
      canvas: canvas,
      onRuntimeInitialized: () => {
        console.log('wasm initialized!!!');
      },
    };

    const runtime = await OccApp(config);
    self.OccViewer = runtime;
    console.log('runtime : ', runtime);

    setTimeout(() => {
      window.dispatchEvent(new Event('resize'));
    }, 0);

    canvas.focus();
  }

  private onOpenCAD(event: any): void {
    const self = this;

    console.log('onOpenCAD : ', event);

    const input = document.createElement('input');
    input.type = 'file';
    input.accept = self.fileformat;
    input.multiple = false;
    input.onchange = () => {
      if (input.files instanceof FileList && input.files?.length > 0) {
        const file: File = input.files[0];
        self.handleFile(
          file
        );
      }
    };
    input.click();
  }

  private handleFile(file: File) {
    const self = this;

    self.selectedFile = file;

    const reader = new FileReader();
    reader.onload = (event) => {
      console.log(reader.result);
    };
    reader.readAsArrayBuffer(file);
  }
}
