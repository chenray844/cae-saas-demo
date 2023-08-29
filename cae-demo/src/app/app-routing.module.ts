import { NgModule } from '@angular/core';
import { BrowserModule } from '@angular/platform-browser';
import { BrowserAnimationsModule } from '@angular/platform-browser/animations';
import { RouterModule, Routes } from '@angular/router';
import { DockModule } from 'primeng/dock';
import { GeometryComponent } from './components/geometry/geometry.component';

const routes: Routes = [
  { title: 'CAE-Demo', path: '', pathMatch: 'full', redirectTo: 'geometry' },
  { title: 'CAE-Geom Viewer [OCCT]', path: 'geometry', pathMatch: 'full', component: GeometryComponent },
];

@NgModule({
  imports: [
    BrowserModule,
    BrowserAnimationsModule,
    DockModule,
    RouterModule.forRoot(routes),
  ],
  exports: [RouterModule],
  declarations: [
    GeometryComponent
  ]
})
export class AppRoutingModule { }
