/* Bounded offline CLI; no writes, assets, browser or game launch. GPL-2.0-or-later. */
import fs from 'node:fs';
import {parseLayout} from './model.mjs';
import {exportTranslationReport} from './translation.mjs';

if(process.argv.length !== 3)
{
	console.error('Usage: node validate-layout.mjs <layout.json>');
	process.exitCode = 2;
}
else
{
	let descriptor;
	try
	{
		descriptor = fs.openSync(process.argv[2], fs.constants.O_RDONLY | fs.constants.O_NONBLOCK);
		const stat = fs.fstatSync(descriptor);
		if(!stat.isFile() || stat.size > 1024 * 1024) throw new Error('Bounded regular file required');
		const buffer = Buffer.alloc(1024 * 1024 + 1);
		let length = 0, read = 1;
		while(read && length < buffer.length) { read = fs.readSync(descriptor, buffer, length, buffer.length - length, null); length += read; }
		if(length > 1024 * 1024) throw new Error('Layout exceeds bound');
		const source = new TextDecoder('utf-8', {fatal: true}).decode(buffer.subarray(0, length));
		process.stdout.write(exportTranslationReport(parseLayout(source)));
	}
	catch
	{
		console.error('Layout refused: unreadable/bounded file, UTF-8, JSON or layout validation failed.');
		process.exitCode = 2; // Do not print file paths or malformed private input excerpts.
	}
	finally { if(descriptor !== undefined) fs.closeSync(descriptor); }
}
