import { Component, OnInit, OnDestroy, AfterViewInit } from '@angular/core';
import { MenuItem } from 'primeng/api';
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

  constructor() {
    const self = this;
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
    const container = <HTMLDivElement>document.getElementById('occt_container');
    container.appendChild(canvas);

    const config = {
      canvas: canvas,
      onRuntimeInitialized: () => {
        console.log('wasm initialized!!!');
      },
    };

    const runtime = await OccApp(config);
    console.log('runtime : ', runtime);
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
