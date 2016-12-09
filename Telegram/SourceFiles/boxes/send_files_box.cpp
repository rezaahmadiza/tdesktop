/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "boxes/send_files_box.h"

#include "lang.h"
#include "localstorage.h"
#include "mainwidget.h"
#include "history/history_media_types.h"
#include "ui/filedialog.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/input_fields.h"
#include "styles/style_history.h"
#include "styles/style_boxes.h"

namespace {

constexpr auto kMinPreviewWidth = 20;

} // namespace

SendFilesBox::SendFilesBox(const QString &filepath, QImage image, CompressConfirm compressed, bool animated) : AbstractBox(st::boxWideWidth)
, _files(filepath)
, _image(image)
, _compressConfirm(compressed)
, _animated(image.isNull() ? false : animated)
, _caption(this, st::confirmCaptionArea, lang(lng_photo_caption))
, _send(this, lang(lng_send_button), st::defaultBoxButton)
, _cancel(this, lang(lng_cancel), st::cancelBoxButton) {
	if (!image.isNull()) {
		if (!_animated && _compressConfirm == CompressConfirm::None) {
			auto originalWidth = image.width();
			auto originalHeight = image.height();
			auto thumbWidth = st::msgFileThumbSize;
			if (originalWidth > originalHeight) {
				thumbWidth = (originalWidth * st::msgFileThumbSize) / originalHeight;
			}
			auto options = Images::Option::Smooth | Images::Option::RoundedSmall | Images::Option::RoundedTopLeft | Images::Option::RoundedTopRight | Images::Option::RoundedBottomLeft | Images::Option::RoundedBottomRight;
			_fileThumb = Images::pixmap(image, thumbWidth * cIntRetinaFactor(), 0, options, st::msgFileThumbSize, st::msgFileThumbSize);
		} else {
			auto maxW = 0;
			auto maxH = 0;
			if (_animated) {
				auto limitW = width() - st::boxPhotoPadding.left() - st::boxPhotoPadding.right();
				auto limitH = st::confirmMaxHeight;
				maxW = qMax(image.width(), 1);
				maxH = qMax(image.height(), 1);
				if (maxW * limitH > maxH * limitW) {
					if (maxW < limitW) {
						maxH = maxH * limitW / maxW;
						maxW = limitW;
					}
				} else {
					if (maxH < limitH) {
						maxW = maxW * limitH / maxH;
						maxH = limitH;
					}
				}
				image = Images::prepare(image, maxW * cIntRetinaFactor(), maxH * cIntRetinaFactor(), Images::Option::Smooth | Images::Option::Blurred, maxW, maxH);
			}
			auto originalWidth = image.width();
			auto originalHeight = image.height();
			if (!originalWidth || !originalHeight) {
				originalWidth = originalHeight = 1;
			}
			_previewWidth = width() - st::boxPhotoPadding.left() - st::boxPhotoPadding.right();
			if (image.width() < _previewWidth) {
				_previewWidth = qMax(image.width(), kMinPreviewWidth);
			}
			auto maxthumbh = qMin(qRound(1.5 * _previewWidth), st::confirmMaxHeight);
			_previewHeight = qRound(originalHeight * float64(_previewWidth) / originalWidth);
			if (_previewHeight > maxthumbh) {
				_previewWidth = qRound(_previewWidth * float64(maxthumbh) / _previewHeight);
				accumulate_max(_previewWidth, kMinPreviewWidth);
				_previewHeight = maxthumbh;
			}
			_previewLeft = (width() - _previewWidth) / 2;

			image = std_::move(image).scaled(_previewWidth * cIntRetinaFactor(), _previewHeight * cIntRetinaFactor(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
			image = Images::prepareOpaque(std_::move(image));
			_preview = App::pixmapFromImageInPlace(std_::move(image));
			_preview.setDevicePixelRatio(cRetinaFactor());
		}
	}
	if (_preview.isNull()) {
		if (filepath.isEmpty()) {
			auto filename = filedialogDefaultName(qsl("image"), qsl(".png"), QString(), true);
			_nameText.setText(st::semiboldFont, filename, _textNameOptions);
			_statusText = qsl("%1x%2").arg(_image.width()).arg(_image.height());
			_statusWidth = qMax(_nameText.maxWidth(), st::normalFont->width(_statusText));
			_fileIsImage = true;
		} else {
			auto fileinfo = QFileInfo(filepath);
			auto filename = fileinfo.fileName();
			_nameText.setText(st::semiboldFont, filename, _textNameOptions);
			_statusText = formatSizeText(fileinfo.size());
			_statusWidth = qMax(_nameText.maxWidth(), st::normalFont->width(_statusText));
			_fileIsImage = fileIsImage(filename, mimeTypeForFile(fileinfo).name());
		}
	}

	setup();
}

SendFilesBox::SendFilesBox(const QStringList &files, CompressConfirm compressed) : AbstractBox(st::boxWideWidth)
, _files(files)
, _compressConfirm(compressed)
, _caption(this, st::confirmCaptionArea, lang(lng_photos_comment))
, _send(this, lang(lng_send_button), st::defaultBoxButton)
, _cancel(this, lang(lng_cancel), st::cancelBoxButton) {
	updateTitleText();

	setup();
}

SendFilesBox::SendFilesBox(const QString &phone, const QString &firstname, const QString &lastname) : AbstractBox(st::boxWideWidth)
, _contactPhone(phone)
, _contactFirstName(firstname)
, _contactLastName(lastname)
, _send(this, lang(lng_send_button), st::defaultBoxButton)
, _cancel(this, lang(lng_cancel), st::cancelBoxButton) {
	_nameText.setText(st::semiboldFont, lng_full_name(lt_first_name, _contactFirstName, lt_last_name, _contactLastName), _textNameOptions);
	_statusText = _contactPhone;
	_statusWidth = qMax(_nameText.maxWidth(), st::normalFont->width(_statusText));

	setup();
}

void SendFilesBox::setup() {
	_send->setClickedCallback([this] { onSend(); });
	_cancel->setClickedCallback([this] { onClose(); });

	if (_compressConfirm != CompressConfirm::None) {
		auto compressed = (_compressConfirm == CompressConfirm::Auto) ? cCompressPastedImage() : (_compressConfirm == CompressConfirm::Yes);
		auto text = lng_send_images_compress(lt_count, _files.size());
		_compressed.create(this, text, compressed, st::defaultBoxCheckbox);
		connect(_compressed, SIGNAL(changed()), this, SLOT(onCompressedChange()));
	}
	if (_caption) {
		_caption->setMaxLength(MaxPhotoCaption);
		_caption->setCtrlEnterSubmit(Ui::CtrlEnterSubmitBoth);
		connect(_caption, SIGNAL(resized()), this, SLOT(onCaptionResized()));
		connect(_caption, SIGNAL(submitted(bool)), this, SLOT(onSend(bool)));
		connect(_caption, SIGNAL(cancelled()), this, SLOT(onClose()));
	}
	_send->setText(getSendButtonText());
	updateBoxSize();
}

QString SendFilesBox::getSendButtonText() const {
	if (!_contactPhone.isEmpty()) {
		return lang(lng_send_button);
	} else if (_compressed && _compressed->checked()) {
		return lng_send_photos(lt_count, _files.size());
	}
	return lng_send_files(lt_count, _files.size());
}

void SendFilesBox::onCompressedChange() {
	doSetInnerFocus();
	_send->setText(getSendButtonText());
	updateControlsGeometry();
}

void SendFilesBox::onCaptionResized() {
	updateBoxSize();
	updateControlsGeometry();
	update();
}

void SendFilesBox::updateTitleText() {
	_titleText = (_compressConfirm == CompressConfirm::None) ? lng_send_files_selected(lt_count, _files.size()) : lng_send_images_selected(lt_count, _files.size());
	update();
}

void SendFilesBox::updateBoxSize() {
	auto newHeight = 0;
	if (!_preview.isNull()) {
		newHeight += st::boxPhotoPadding.top() + _previewHeight;
	} else if (!_fileThumb.isNull()) {
		newHeight += st::boxPhotoPadding.top() + st::msgFileThumbPadding.top() + st::msgFileThumbSize + st::msgFileThumbPadding.bottom();
	} else if (_files.size() > 1) {
		newHeight += titleHeight();
	} else {
		newHeight += st::boxPhotoPadding.top() + st::msgFilePadding.top() + st::msgFileSize + st::msgFilePadding.bottom();
	}
	if (_compressed) {
		newHeight += st::boxPhotoCompressedSkip + _compressed->heightNoMargins();
	}
	if (_caption) {
		newHeight += st::boxPhotoCaptionSkip + _caption->height();
	}
	newHeight += st::boxButtonPadding.top() + _send->height() + st::boxButtonPadding.bottom();
	setMaxHeight(newHeight);
}

void SendFilesBox::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return) {
		onSend((e->modifiers().testFlag(Qt::ControlModifier) || e->modifiers().testFlag(Qt::MetaModifier)) && e->modifiers().testFlag(Qt::ShiftModifier));
	} else {
		AbstractBox::keyPressEvent(e);
	}
}

void SendFilesBox::paintEvent(QPaintEvent *e) {
	AbstractBox::paintEvent(e);

	Painter p(this);

	if (!_titleText.isEmpty()) {
		p.setFont(st::boxPhotoTitleFont);
		p.setPen(st::boxTitleFg);
		p.drawTextLeft(st::boxPhotoTitlePosition.x(), st::boxPhotoTitlePosition.y(), width(), _titleText);
	}

	if (!_preview.isNull()) {
		if (_previewLeft > st::boxPhotoPadding.left()) {
			p.fillRect(st::boxPhotoPadding.left(), st::boxPhotoPadding.top(), _previewLeft - st::boxPhotoPadding.left(), _previewHeight, st::confirmBg);
		}
		if (_previewLeft + _previewWidth < width() - st::boxPhotoPadding.right()) {
			p.fillRect(_previewLeft + _previewWidth, st::boxPhotoPadding.top(), width() - st::boxPhotoPadding.right() - _previewLeft - _previewWidth, _previewHeight, st::confirmBg);
		}
		p.drawPixmap(_previewLeft, st::boxPhotoPadding.top(), _preview);
		if (_animated) {
			auto inner = QRect(_previewLeft + (_previewWidth - st::msgFileSize) / 2, st::boxPhotoPadding.top() + (_previewHeight - st::msgFileSize) / 2, st::msgFileSize, st::msgFileSize);
			p.setPen(Qt::NoPen);
			p.setBrush(st::msgDateImgBg);

			{
				PainterHighQualityEnabler hq(p);
				p.drawEllipse(inner);
			}

			auto icon = &st::historyFileInPlay;
			icon->paintInCenter(p, inner);
		}
	} else if (_files.size() < 2) {
		auto w = width() - st::boxPhotoPadding.left() - st::boxPhotoPadding.right();
		auto h = _fileThumb.isNull() ? (st::msgFilePadding.top() + st::msgFileSize + st::msgFilePadding.bottom()) : (st::msgFileThumbPadding.top() + st::msgFileThumbSize + st::msgFileThumbPadding.bottom());
		auto nameleft = 0, nametop = 0, nameright = 0, statustop = 0, linktop = 0;
		if (_fileThumb.isNull()) {
			nameleft = st::msgFilePadding.left() + st::msgFileSize + st::msgFilePadding.right();
			nametop = st::msgFileNameTop;
			nameright = st::msgFilePadding.left();
			statustop = st::msgFileStatusTop;
		} else {
			nameleft = st::msgFileThumbPadding.left() + st::msgFileThumbSize + st::msgFileThumbPadding.right();
			nametop = st::msgFileThumbNameTop;
			nameright = st::msgFileThumbPadding.left();
			statustop = st::msgFileThumbStatusTop;
			linktop = st::msgFileThumbLinkTop;
		}
		auto namewidth = w - nameleft - (_fileThumb.isNull() ? st::msgFilePadding.left() : st::msgFileThumbPadding.left());
		int32 x = (width() - w) / 2, y = st::boxPhotoPadding.top();

		App::roundRect(p, x, y, w, h, st::msgOutBg, MessageOutCorners, &st::msgOutShadow);

		if (_fileThumb.isNull()) {
			if (_contactPhone.isNull()) {
				QRect inner(rtlrect(x + st::msgFilePadding.left(), y + st::msgFilePadding.top(), st::msgFileSize, st::msgFileSize, width()));
				p.setPen(Qt::NoPen);
				p.setBrush(st::msgFileOutBg);

				{
					PainterHighQualityEnabler hq(p);
					p.drawEllipse(inner);
				}

				auto &icon = _fileIsImage ? st::historyFileOutImage : st::historyFileOutDocument;
				icon.paintInCenter(p, inner);
			} else {
				p.drawPixmapLeft(x + st::msgFilePadding.left(), y + st::msgFilePadding.top(), width(), userDefPhoto(1)->pixCircled(st::msgFileSize));
			}
		} else {
			QRect rthumb(rtlrect(x + st::msgFileThumbPadding.left(), y + st::msgFileThumbPadding.top(), st::msgFileThumbSize, st::msgFileThumbSize, width()));
			p.drawPixmap(rthumb.topLeft(), _fileThumb);
		}
		p.setFont(st::semiboldFont);
		p.setPen(st::historyFileNameOutFg);
		_nameText.drawLeftElided(p, x + nameleft, y + nametop, namewidth, width());

		auto &status = st::mediaOutFg;
		p.setFont(st::normalFont);
		p.setPen(status);
		p.drawTextLeft(x + nameleft, y + statustop, width(), _statusText);
	}
}

void SendFilesBox::resizeEvent(QResizeEvent *e) {
	updateControlsGeometry();
	AbstractBox::resizeEvent(e);
}

void SendFilesBox::updateControlsGeometry() {
	_send->moveToRight(st::boxButtonPadding.right(), height() - st::boxButtonPadding.bottom() - _send->height());
	_cancel->moveToRight(st::boxButtonPadding.right() + _send->width() + st::boxButtonPadding.left(), _send->y());
	auto bottom = _send->y() - st::boxButtonPadding.top();
	if (_caption) {
		_caption->resize(st::boxWideWidth - st::boxPhotoPadding.left() - st::boxPhotoPadding.right(), _caption->height());
		_caption->moveToLeft(st::boxPhotoPadding.left(), bottom - _caption->height());
		bottom -= st::boxPhotoCaptionSkip + _caption->height();
	}
	if (_compressed) {
		_compressed->moveToLeft(st::boxPhotoPadding.left(), bottom - _compressed->heightNoMargins());
		bottom -= st::boxPhotoCompressedSkip + _compressed->heightNoMargins();
	}
}

void SendFilesBox::doSetInnerFocus() {
	if (!_caption || _caption->isHidden()) {
		setFocus();
	} else {
		_caption->setFocus();
	}
}

void SendFilesBox::onSend(bool ctrlShiftEnter) {
	if (_compressed && _compressConfirm == CompressConfirm::Auto && _compressed->checked() != cCompressPastedImage()) {
		cSetCompressPastedImage(_compressed->checked());
		Local::writeUserSettings();
	}
	_confirmed = true;
	if (_confirmedCallback) {
		auto compressed = _compressed ? _compressed->checked() : false;
		auto caption = _caption ? prepareText(_caption->getLastText(), true) : QString();
		_confirmedCallback(_files, compressed, caption, ctrlShiftEnter);
	}
	onClose();
}

void SendFilesBox::closePressed() {
	if (!_confirmed && _cancelledCallback) {
		_cancelledCallback();
	}
}

EditCaptionBox::EditCaptionBox(HistoryItem *msg) : AbstractBox(st::boxWideWidth)
, _msgId(msg->fullId())
, _animated(false)
, _photo(false)
, _doc(false)
, _save(this, lang(lng_settings_save), st::defaultBoxButton)
, _cancel(this, lang(lng_cancel), st::cancelBoxButton)
, _thumbx(0)
, _thumby(0)
, _thumbw(0)
, _thumbh(0)
, _statusw(0)
, _isImage(false)
, _previewCancelled(false)
, _saveRequestId(0) {
	connect(_save, SIGNAL(clicked()), this, SLOT(onSave()));
	connect(_cancel, SIGNAL(clicked()), this, SLOT(onClose()));

	QSize dimensions;
	ImagePtr image;
	QString caption;
	DocumentData *doc = 0;
	if (HistoryMedia *media = msg->getMedia()) {
		HistoryMediaType t = media->type();
		switch (t) {
		case MediaTypeGif: {
			_animated = true;
			doc = static_cast<HistoryGif*>(media)->getDocument();
			dimensions = doc->dimensions;
			image = doc->thumb;
		} break;

		case MediaTypePhoto: {
			_photo = true;
			PhotoData *photo = static_cast<HistoryPhoto*>(media)->photo();
			dimensions = QSize(photo->full->width(), photo->full->height());
			image = photo->full;
		} break;

		case MediaTypeVideo: {
			_animated = true;
			doc = static_cast<HistoryVideo*>(media)->getDocument();
			dimensions = doc->dimensions;
			image = doc->thumb;
		} break;

		case MediaTypeFile:
		case MediaTypeMusicFile:
		case MediaTypeVoiceFile: {
			_doc = true;
			doc = static_cast<HistoryDocument*>(media)->getDocument();
			image = doc->thumb;
		} break;
		}
		caption = media->getCaption().text;
	}
	if ((!_animated && (dimensions.isEmpty() || doc)) || image->isNull()) {
		_animated = false;
		if (image->isNull()) {
			_thumbw = 0;
		} else {
			int32 tw = image->width(), th = image->height();
			if (tw > th) {
				_thumbw = (tw * st::msgFileThumbSize) / th;
			} else {
				_thumbw = st::msgFileThumbSize;
			}
			auto options = Images::Option::Smooth | Images::Option::RoundedSmall | Images::Option::RoundedTopLeft | Images::Option::RoundedTopRight | Images::Option::RoundedBottomLeft | Images::Option::RoundedBottomRight;
			_thumb = Images::pixmap(image->pix().toImage(), _thumbw * cIntRetinaFactor(), 0, options, st::msgFileThumbSize, st::msgFileThumbSize);
		}

		if (doc) {
			if (doc->voice()) {
				_name.setText(st::semiboldFont, lang(lng_media_audio), _textNameOptions);
			} else {
				_name.setText(st::semiboldFont, documentName(doc), _textNameOptions);
			}
			_status = formatSizeText(doc->size);
			_statusw = qMax(_name.maxWidth(), st::normalFont->width(_status));
			_isImage = doc->isImage();
		}
	} else {
		int32 maxW = 0, maxH = 0;
		if (_animated) {
			int32 limitW = width() - st::boxPhotoPadding.left() - st::boxPhotoPadding.right();
			int32 limitH = st::confirmMaxHeight;
			maxW = qMax(dimensions.width(), 1);
			maxH = qMax(dimensions.height(), 1);
			if (maxW * limitH > maxH * limitW) {
				if (maxW < limitW) {
					maxH = maxH * limitW / maxW;
					maxW = limitW;
				}
			} else {
				if (maxH < limitH) {
					maxW = maxW * limitH / maxH;
					maxH = limitH;
				}
			}
			_thumb = image->pixNoCache(maxW * cIntRetinaFactor(), maxH * cIntRetinaFactor(), Images::Option::Smooth | Images::Option::Blurred, maxW, maxH);
		} else {
			maxW = dimensions.width();
			maxH = dimensions.height();
			_thumb = image->pixNoCache(maxW * cIntRetinaFactor(), maxH * cIntRetinaFactor(), Images::Option::Smooth, maxW, maxH);
		}
		int32 tw = _thumb.width(), th = _thumb.height();
		if (!tw || !th) {
			tw = th = 1;
		}
		_thumbw = width() - st::boxPhotoPadding.left() - st::boxPhotoPadding.right();
		if (_thumb.width() < _thumbw) {
			_thumbw = (_thumb.width() > 20) ? _thumb.width() : 20;
		}
		int32 maxthumbh = qMin(qRound(1.5 * _thumbw), int(st::confirmMaxHeight));
		_thumbh = qRound(th * float64(_thumbw) / tw);
		if (_thumbh > maxthumbh) {
			_thumbw = qRound(_thumbw * float64(maxthumbh) / _thumbh);
			_thumbh = maxthumbh;
			if (_thumbw < 10) {
				_thumbw = 10;
			}
		}
		_thumbx = (width() - _thumbw) / 2;

		_thumb = App::pixmapFromImageInPlace(_thumb.toImage().scaled(_thumbw * cIntRetinaFactor(), _thumbh * cIntRetinaFactor(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
		_thumb.setDevicePixelRatio(cRetinaFactor());
	}

	if (_animated || _photo || _doc) {
		_field.create(this, st::confirmCaptionArea, lang(lng_photo_caption), caption);
		_field->setMaxLength(MaxPhotoCaption);
		_field->setCtrlEnterSubmit(Ui::CtrlEnterSubmitBoth);
	} else {
		auto original = msg->originalText();
		QString text = textApplyEntities(original.text, original.entities);
		_field.create(this, st::editTextArea, lang(lng_photo_caption), text);
//		_field->setMaxLength(MaxMessageSize); // entities can make text in input field larger but still valid
		_field->setCtrlEnterSubmit(cCtrlEnter() ? Ui::CtrlEnterSubmitCtrlEnter : Ui::CtrlEnterSubmitEnter);
	}
	updateBoxSize();
	connect(_field, SIGNAL(submitted(bool)), this, SLOT(onSave(bool)));
	connect(_field, SIGNAL(cancelled()), this, SLOT(onClose()));
	connect(_field, SIGNAL(resized()), this, SLOT(onCaptionResized()));

	QTextCursor c(_field->textCursor());
	c.movePosition(QTextCursor::End);
	_field->setTextCursor(c);
}

bool EditCaptionBox::captionFound() const {
	return _animated || _photo || _doc;
}

void EditCaptionBox::onCaptionResized() {
	updateBoxSize();
	resizeEvent(0);
	update();
}

void EditCaptionBox::updateBoxSize() {
	auto bottomh = st::boxPhotoCaptionSkip + _field->height() + st::normalFont->height + st::boxButtonPadding.top() + _save->height() + st::boxButtonPadding.bottom();
	if (_photo || _animated) {
		setMaxHeight(st::boxPhotoPadding.top() + _thumbh + bottomh);
	} else if (_thumbw) {
		setMaxHeight(st::boxPhotoPadding.top() + 0 + st::msgFileThumbSize + 0 + bottomh);
	} else if (_doc) {
		setMaxHeight(st::boxPhotoPadding.top() + 0 + st::msgFileSize + 0 + bottomh);
	} else {
		setMaxHeight(st::boxPhotoPadding.top() + st::boxTitleFont->height + bottomh);
	}
}

void EditCaptionBox::paintEvent(QPaintEvent *e) {
	AbstractBox::paintEvent(e);

	Painter p(this);

	if (_photo || _animated) {
		if (_thumbx > st::boxPhotoPadding.left()) {
			p.fillRect(st::boxPhotoPadding.left(), st::boxPhotoPadding.top(), _thumbx - st::boxPhotoPadding.left(), _thumbh, st::confirmBg->b);
		}
		if (_thumbx + _thumbw < width() - st::boxPhotoPadding.right()) {
			p.fillRect(_thumbx + _thumbw, st::boxPhotoPadding.top(), width() - st::boxPhotoPadding.right() - _thumbx - _thumbw, _thumbh, st::confirmBg->b);
		}
		p.drawPixmap(_thumbx, st::boxPhotoPadding.top(), _thumb);
		if (_animated) {
			QRect inner(_thumbx + (_thumbw - st::msgFileSize) / 2, st::boxPhotoPadding.top() + (_thumbh - st::msgFileSize) / 2, st::msgFileSize, st::msgFileSize);
			p.setPen(Qt::NoPen);
			p.setBrush(st::msgDateImgBg);

			{
				PainterHighQualityEnabler hq(p);
				p.drawEllipse(inner);
			}
			
			auto icon = &st::historyFileInPlay;
			icon->paintInCenter(p, inner);
		}
	} else if (_doc) {
		int32 w = width() - st::boxPhotoPadding.left() - st::boxPhotoPadding.right();
		int32 h = _thumbw ? (0 + st::msgFileThumbSize + 0) : (0 + st::msgFileSize + 0);
		int32 nameleft = 0, nametop = 0, nameright = 0, statustop = 0;
		if (_thumbw) {
			nameleft = 0 + st::msgFileThumbSize + st::msgFileThumbPadding.right();
			nametop = st::msgFileThumbNameTop - st::msgFileThumbPadding.top();
			nameright = 0;
			statustop = st::msgFileThumbStatusTop - st::msgFileThumbPadding.top();
		} else {
			nameleft = 0 + st::msgFileSize + st::msgFilePadding.right();
			nametop = st::msgFileNameTop - st::msgFilePadding.top();
			nameright = 0;
			statustop = st::msgFileStatusTop - st::msgFilePadding.top();
		}
		int32 namewidth = w - nameleft - 0;
		if (namewidth > _statusw) {
			//w -= (namewidth - _statusw);
			//namewidth = _statusw;
		}
		int32 x = (width() - w) / 2, y = st::boxPhotoPadding.top();

//		App::roundRect(p, x, y, w, h, st::msgInBg, MessageInCorners, &st::msgInShadow);

		if (_thumbw) {
			QRect rthumb(rtlrect(x + 0, y + 0, st::msgFileThumbSize, st::msgFileThumbSize, width()));
			p.drawPixmap(rthumb.topLeft(), _thumb);
		} else {
			QRect inner(rtlrect(x + 0, y + 0, st::msgFileSize, st::msgFileSize, width()));
			p.setPen(Qt::NoPen);
			p.setBrush(st::msgFileInBg);

			{
				PainterHighQualityEnabler hq(p);
				p.drawEllipse(inner);
			}

			auto icon = &(_isImage ? st::historyFileInImage : st::historyFileInDocument);
			icon->paintInCenter(p, inner);
		}
		p.setFont(st::semiboldFont);
		p.setPen(st::historyFileNameInFg);
		_name.drawLeftElided(p, x + nameleft, y + nametop, namewidth, width());

		auto &status = st::mediaInFg;
		p.setFont(st::normalFont);
		p.setPen(status);
		p.drawTextLeft(x + nameleft, y + statustop, width(), _status);
	} else {
		p.setFont(st::boxTitleFont);
		p.setPen(st::boxTextFg);
		p.drawTextLeft(_field->x(), st::boxPhotoPadding.top(), width(), lang(lng_edit_message));
	}

	if (!_error.isEmpty()) {
		p.setFont(st::normalFont);
		p.setPen(st::boxTextFgError);
		p.drawTextLeft(_field->x(), _field->y() + _field->height() + (st::boxButtonPadding.top() / 2), width(), _error);
	}
}

void EditCaptionBox::resizeEvent(QResizeEvent *e) {
	_save->moveToRight(st::boxButtonPadding.right(), height() - st::boxButtonPadding.bottom() - _save->height());
	_cancel->moveToRight(st::boxButtonPadding.right() + _save->width() + st::boxButtonPadding.left(), _save->y());
	_field->resize(st::boxWideWidth - st::boxPhotoPadding.left() - st::boxPhotoPadding.right(), _field->height());
	_field->moveToLeft(st::boxPhotoPadding.left(), _save->y() - st::boxButtonPadding.top() - st::normalFont->height - _field->height());
	AbstractBox::resizeEvent(e);
}

void EditCaptionBox::doSetInnerFocus() {
	_field->setFocus();
}

void EditCaptionBox::onSave(bool ctrlShiftEnter) {
	if (_saveRequestId) return;

	HistoryItem *item = App::histItemById(_msgId);
	if (!item) {
		_error = lang(lng_edit_deleted);
		update();
		return;
	}

	MTPmessages_EditMessage::Flags flags = MTPmessages_EditMessage::Flag::f_message;
	if (_previewCancelled) {
		flags |= MTPmessages_EditMessage::Flag::f_no_webpage;
	}
	MTPVector<MTPMessageEntity> sentEntities;
	if (!sentEntities.c_vector().v.isEmpty()) {
		flags |= MTPmessages_EditMessage::Flag::f_entities;
	}
	auto text = prepareText(_field->getLastText(), true);
	_saveRequestId = MTP::send(MTPmessages_EditMessage(MTP_flags(flags), item->history()->peer->input, MTP_int(item->id), MTP_string(text), MTPnullMarkup, sentEntities), rpcDone(&EditCaptionBox::saveDone), rpcFail(&EditCaptionBox::saveFail));
}

void EditCaptionBox::saveDone(const MTPUpdates &updates) {
	_saveRequestId = 0;
	onClose();
	if (App::main()) {
		App::main()->sentUpdatesReceived(updates);
	}
}

bool EditCaptionBox::saveFail(const RPCError &error) {
	if (MTP::isDefaultHandledError(error)) return false;

	_saveRequestId = 0;
	QString err = error.type();
	if (err == qstr("MESSAGE_ID_INVALID") || err == qstr("CHAT_ADMIN_REQUIRED") || err == qstr("MESSAGE_EDIT_TIME_EXPIRED")) {
		_error = lang(lng_edit_error);
	} else if (err == qstr("MESSAGE_NOT_MODIFIED")) {
		onClose();
		return true;
	} else if (err == qstr("MESSAGE_EMPTY")) {
		_field->setFocus();
		_field->showError();
	} else {
		_error = lang(lng_edit_error);
	}
	update();
	return true;
}